/* ar.c - ARAM and the ARAM DMA queue.
 *
 * ARAM is a host buffer at a fixed VA; ARAM "addresses" are offsets into it,
 * which is what PC_IS_ARAM_ADDR (below 16 MB) relies on. It is committed on
 * demand (xhw_reserve_lazy): only the parts the game fills cost Xbox RAM. ARQ transfers
 * with a callback are copied by a worker thread and their callbacks run on
 * the game thread (below, "ARQ"); loaders spin on flags those callbacks
 * set, delivering them as they spin (ARQPoll, interrupts re-enabled).
 *
 * Disc-backed pages. Melee fills ARAM with files it preloads, and with the
 * 24 MB MEM1 that is more than the Xbox has once the game has been running
 * a while. A copy into ARAM that the DVD worker just read from the disc
 * image (the devcom relay: disc -> relay buffer -> ARAM, posted from the
 * read's callback) only records, per 4 KB page, where on the image those
 * bytes are. A 64 KB chunk whose pages are all on the disc gives its memory
 * back; reading it back into MEM1 reads the image; a CPU touch (the audio
 * mixer reads samples in ARAM) faults, and the chunk is committed and
 * filled from the image (fill_chunk). Everything else is kept in memory as
 * before.
 *
 * The swap file. Prime's ARAM also holds what was never on the disc as is
 * (the world PAKs' resource tables CPakFile stashes, sample data decoded in
 * MEM1): ~4 MB in play. When the Xbox runs low (xhw_mem_tick, or a commit
 * that failed: aram_reclaim), whole chunks go back to the kernel: those all
 * on the disc at once, then others written to a file in the save folder
 * first (one 64 KB slot per chunk, given at its first swap-out), their
 * pages marked as living there. A touch or a read brings them back the way
 * disc pages come back. */
#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xsdk.h"

#ifndef XSDK_ARAM_VA
#define XSDK_ARAM_VA 0x12000000u
#endif
#define ARAM_SIZE (16u * 1024 * 1024)
#define ARAM_USER_BASE 0x4000u   /* the SDK keeps the first 16 KB for the DSP */

#define PAGE 4096u
#define NPAGES (ARAM_SIZE / PAGE)
#define CHUNK XHW_LAZY_CHUNK
#define PER_CHUNK (CHUNK / PAGE)
#define NO_DISC 0xFFFFFFFFu
#define SWAP 0x80000000u   /* s_disc: swap file offset | SWAP (GameCube images are < 2 GB) */
#define NCHUNKS (ARAM_SIZE / CHUNK)

static u8* s_aram;
static u32 s_disc[NPAGES];   /* page -> disc image offset of its bytes, or NO_DISC */
static u32 s_disc_pages;
static xhw_mutex* s_lock;    /* s_disc and chunk states; recursive */
static u32 s_top = ARAM_USER_BASE;
static u32* s_stack;
static u32 s_stack_n, s_stack_max;
static ARQCallback s_dma_cb;
static void* s_swap;          /* the swap file, or NULL */
static u16 s_slot[NCHUNKS];   /* chunk -> its swap slot + 1, 0: none yet */
static u32 s_slots, s_swap_pages, s_cursor;
static u32 s_swapped_out, s_swap_fills;
static int s_swap_full;      /* writes kept failing (disk full?): no more swap-outs */
static u32 s_swap_fails;     /* in a row: one can be a passing shortage of the kernel's own memory */
/* each chunk's last write or fill, in frames: a chunk used in the last
 * ARAM_HOT_FRAMES (a music stream's buffer, samples playing) is swapped out
 * only when nothing colder is left, or under a failed commit */
#define ARAM_HOT_FRAMES 120
static u32 s_touch[NCHUNKS];
#if XHW_TEST_BUILD
/* test builds: each swapped-out chunk's hash, checked when it comes back
 * (0: none, or ARAM was written there since) */
static u32 s_hash[NCHUNKS];
static u32 s_hash_bad;
static u32 chunk_hash(const u8* p) {
    u32 h = 2166136261u, i;
    for (i = 0; i < CHUNK; i += 4) h = (h ^ *(const u32*)(p + i)) * 16777619u;
    return h | 1;
}
#endif

static void fill_chunk(void* chunk);
static u32 aram_reclaim(u32 want_kb, int urgent);

static void aram_map(void) {
    u32 i;
    if (s_aram) return;
    for (i = 0; i < NPAGES; i++) s_disc[i] = NO_DISC;
    s_lock = xhw_mutex_create();
    s_aram = (u8*)xhw_reserve_lazy(XSDK_ARAM_VA, ARAM_SIZE);
    if (!s_aram) xhw_fatal("Out of memory", "Could not reserve ARAM.");
    xhw_lazy_set_fill(s_aram, fill_chunk);
    {
        char path[96];
        snprintf(path, sizeof path, "%saram.swp", xhw_save_dir());
        s_swap = xhw_file_create_rw(path);
        if (!s_swap) xhw_logf("[AR] no swap file (%s)", path);
    }
    xhw_mem_add_reclaim(aram_reclaim, 0);
}

u32 xsdk_aram_disc_kb(void) { return (s_disc_pages - s_swap_pages) * (PAGE / 1024); }
u32 xsdk_aram_swap_kb(void) { return s_swap_pages * (PAGE / 1024); }

static void set_disc(u32 page, u32 off) {
    u32 old = s_disc[page];
    if ((old == NO_DISC) != (off == NO_DISC)) s_disc_pages += off == NO_DISC ? (u32)-1 : 1u;
    if ((old != NO_DISC && (old & SWAP)) != (off != NO_DISC && (off & SWAP)))
        s_swap_pages += off != NO_DISC && (off & SWAP) ? 1u : (u32)-1;
    s_disc[page] = off;
}

/* a page's bytes from wherever they live: the image or the swap file */
static int backing_read(u32 at, u8* dst, u32 len) {
    if (at & SWAP) return s_swap && xhw_file_read(s_swap, at & ~SWAP, dst, len);
    return xsdk_dvd_image_read(at, dst, len);
}

/* pages [p, p + n) from the image into dst, runs of consecutive pages in
 * one read; pages not on the disc were never written: zero */
static void read_pages(u32 p, u32 n, u32 in, u8* dst, u32 len) {
    while (len) {
        u32 run = PAGE - in, k = 1;
        if (run > len) run = len;
        if (s_disc[p] == NO_DISC) {
            memset(dst, 0, run);
        } else {
            while (run < len && k < n && s_disc[p + k] == s_disc[p] + k * PAGE) {
                run += len - run < PAGE ? len - run : PAGE;
                k++;
            }
            if (!backing_read(s_disc[p] + in, dst, run)) {
                xhw_logf("[AR] image read failed: %08x + %u", (unsigned)(s_disc[p] + in), (unsigned)run);
                memset(dst, 0, run);
            }
        }
        dst += run;
        len -= run;
        p += k;
        n -= k;
        in = 0;
    }
}

/* xhw_lazy fill: the chunk was just committed (zeroed). Committed before
 * the lock was ours, so aram_reclaim may have given it back meanwhile (a
 * mixer fault racing a game-thread commit and a reclaim): committed again
 * under the lock, where nothing decommits. */
static void fill_chunk(void* chunk) {
    u32 c = (u32)((u8*)chunk - s_aram) / CHUNK;
    xhw_mutex_lock(s_lock);
    if (!xhw_lazy_is_committed(chunk)) {
        if (!xhw_lazy_commit_pages(chunk)) {
            xhw_logf("[MEM] out of memory refilling ARAM chunk %u (free %u KB)", (unsigned)c, xhw_mem_free_kb());
            xhw_fatal("Out of memory", "The Xbox ran out of memory for the game's main memory or ARAM.");
        }
        s_touch[c] = xsdk_frame_count();
        if (s_slot[c]) s_swap_fills++;
        read_pages(c * PER_CHUNK, PER_CHUNK, 0, (u8*)chunk, CHUNK);
#if XHW_TEST_BUILD
        if (s_hash[c] && chunk_hash((const u8*)chunk) != s_hash[c] && s_hash_bad++ < 16)
            xhw_logf("[AR] swap verify: chunk %u came back different", (unsigned)c);
#endif
    }
#ifdef XSDK_ARAM_VERIFY
    {
        static u32 nfill;
        if ((++nfill & 15) == 1) xhw_logf("[AR] verify: %u chunks filled, %u KB on disc", nfill, xsdk_aram_disc_kb());
    }
#endif
    xhw_mutex_unlock(s_lock);
}

static int chunk_on_disc(u32 c) {
    u32 p;
    for (p = c * PER_CHUNK; p < (c + 1) * PER_CHUNK; p++)
        if (s_disc[p] == NO_DISC) return 0;
    return 1;
}

/* Chunk c (committed) into its swap slot; its pages not on the disc are
 * then marked as in the file. 0 if it can't be written. */
static int swap_out(u32 c) {
    u32 p, off;
    if (!s_swap || s_swap_full) return 0;
    if (!s_slot[c]) {
        if (s_slots >= NCHUNKS) return 0;
        s_slot[c] = (u16)++s_slots;
    }
    off = (u32)(s_slot[c] - 1) * CHUNK;
    if (!xhw_file_write(s_swap, off, s_aram + c * CHUNK, CHUNK)) {
        xhw_logf("[AR] swap write failed: chunk %u", (unsigned)c);
        if (++s_swap_fails >= 4) s_swap_full = 1;   /* what other chunks have in it stays readable */
        for (p = c * PER_CHUNK; p < (c + 1) * PER_CHUNK; p++)   /* this slot may be half written */
            if (s_disc[p] != NO_DISC && (s_disc[p] & SWAP)) set_disc(p, NO_DISC);
        return 0;
    }
    for (p = c * PER_CHUNK; p < (c + 1) * PER_CHUNK; p++)
        if (s_disc[p] == NO_DISC || (s_disc[p] & SWAP)) set_disc(p, SWAP | (off + (p - c * PER_CHUNK) * PAGE));
    s_swapped_out++;
    s_swap_fails = 0;
#if XHW_TEST_BUILD
    s_hash[c] = chunk_hash(s_aram + c * CHUNK);
#endif
    return 1;
}

/* xhw_mem reclaimer: whole chunks back to the kernel, round robin from
 * where the last call stopped; those all on the disc or in the swap file
 * cost nothing, the rest a 64 KB write each. */
/* passes [0, npass): chunks on the disc, then cold ones through the swap
 * file, then (urgent, or still short) hot ones too. s_lock held. */
static u32 reclaim_passes(u32 want_kb, int urgent, u32 npass) {
    u32 got = 0, pass, i, start = s_cursor, now = xsdk_frame_count();
    for (pass = 0; pass < npass && got < want_kb; pass++)
        for (i = 0; i < NCHUNKS && got < want_kb; i++) {
            u32 c = (start + i) % NCHUNKS;
            u8* chunk = s_aram + c * CHUNK;
            int hot = now - s_touch[c] < ARAM_HOT_FRAMES;
            if (!xhw_lazy_is_committed(chunk)) continue;
            if (pass < 2 && hot && !urgent) continue;
            if (!chunk_on_disc(c) && (pass == 0 || !swap_out(c))) continue;
            xhw_lazy_decommit(chunk);
            got += CHUNK / 1024;
            s_cursor = (c + 1) % NCHUNKS;
        }
    return got;
}

static int arq_reclaim_ask(u32 kb);   /* the ARQ worker's swap-outs (below) */

static u32 aram_reclaim(u32 want_kb, int urgent) {
    u32 got;
    if (!s_aram) return 0;
    /* never waited for, urgent or not: the ARQ worker holds it through an
     * image read (seconds on a full stock drive), and xhw_mem_tick runs on
     * the game thread; the next frame tries again */
    if (!xhw_mutex_trylock(s_lock)) return 0;
    /* xhw_mem_tick (the game thread): what costs no I/O here, the swap
     * file's 64 KB writes on the ARQ worker (each a stall on a stock drive:
     * up to 16 a frame) */
    got = reclaim_passes(want_kb, urgent, urgent ? 3 : 1);
    if (!urgent && got < want_kb && !arq_reclaim_ask(want_kb - got)) got += reclaim_passes(want_kb - got, 0, 3);
    xhw_mutex_unlock(s_lock);
    return got;
}

static void xsdk_arq_log(void);

void xsdk_aram_log(void) {
    xhw_logf("[AR] %u KB on the disc, %u KB in the swap file (%u slots); %u chunks swapped out, %u filled back",
             (unsigned)xsdk_aram_disc_kb(), (unsigned)xsdk_aram_swap_kb(), (unsigned)s_slots,
             (unsigned)s_swapped_out, (unsigned)s_swap_fills);
    xsdk_arq_log();
    xsdk_dvd_log();
}

/* main memory -> ARAM; disc: the image offset of src's bytes, or NO_DISC */
static void aram_write(u32 aram, const u8* src, u32 len, u32 disc) {
    u32 end = aram + len;
    while (aram < end) {
        u32 c = aram / CHUNK, cend = (c + 1) * CHUNK < end ? (c + 1) * CHUNK : end, a;
        u8* chunk = s_aram + c * CHUNK;
        s_touch[c] = xsdk_frame_count();
#if XHW_TEST_BUILD
        s_hash[c] = 0;
#endif
        for (a = aram; a < cend;) {
            u32 p = a / PAGE, in = a % PAGE, n = PAGE - in < cend - a ? PAGE - in : cend - a;
            if (disc != NO_DISC && n == PAGE) {
                set_disc(p, disc + (a - aram));
                if (xhw_lazy_is_committed(chunk)) memcpy(s_aram + a, src + (a - aram), n);
            } else {
                xhw_commit(s_aram + a, n);   /* filled first if it was on the disc */
                memcpy(s_aram + a, src + (a - aram), n);
                set_disc(p, NO_DISC);
            }
            a += n;
        }
        if (disc != NO_DISC && xhw_lazy_is_committed(chunk) && chunk_on_disc(c)) xhw_lazy_decommit(chunk);
        src += cend - aram;
        if (disc != NO_DISC) disc += cend - aram;
        aram = cend;
    }
}

/* ARAM -> main memory */
static void aram_read(u32 aram, u8* dst, u32 len) {
    u32 end = aram + len;
    while (aram < end) {
        u32 c = aram / CHUNK, cend = (c + 1) * CHUNK < end ? (c + 1) * CHUNK : end;
        if (xhw_lazy_is_committed(s_aram + c * CHUNK))
            memcpy(dst, s_aram + aram, cend - aram);
        else
            read_pages(aram / PAGE, (cend - 1) / PAGE - aram / PAGE + 1, aram % PAGE, dst, cend - aram);
        dst += cend - aram;
        aram = cend;
    }
}

u8* aurora_aram_base(void) {
    aram_map();
    return s_aram;
}
void* ARGetStorageAddress(void) { return aurora_aram_base(); }
void* xsdk_aram_base(void) { return aurora_aram_base(); }
u32 xsdk_aram_size(void) { return ARAM_SIZE; }

u32 ARInit(u32* stack_index_addr, u32 num_entries) {
    aram_map();
    s_stack = stack_index_addr;
    s_stack_max = num_entries;
    s_stack_n = 0;
    s_top = ARAM_USER_BASE;
    return ARAM_USER_BASE;
}

BOOL ARCheckInit(void) { return s_aram != NULL; }
void ARReset(void) {}
void ARSetSize(void) {}
u32 ARGetBaseAddress(void) { return ARAM_USER_BASE; }
u32 ARGetSize(void) { return ARAM_SIZE; }
u32 ARGetInternalSize(void) { return ARAM_SIZE; }
void ARClear(u32 flag) { (void)flag; }

u32 ARAlloc(u32 length) {
    u32 at = s_top;
    if (s_stack && s_stack_n < s_stack_max) s_stack[s_stack_n++] = length;
    s_top += length;
    return at;
}

u32 ARFree(u32* length) {
    u32 len = 0;
    if (s_stack && s_stack_n) len = s_stack[--s_stack_n];
    s_top -= len;
    if (length) *length = len;
    return s_top;
}

ARQCallback ARRegisterDMACallback(ARQCallback cb) {
    ARQCallback old = s_dma_cb;
    s_dma_cb = cb;
    return old;
}

u32 ARGetDMAStatus(void) { return 0; }

/* type 0: main memory -> ARAM; 1: ARAM -> main memory. disc (type 0): the
 * image offset of mram's bytes (xsdk_dvd_disc_source, looked up where the
 * request is posted: it holds only inside the DVD read's callback), or
 * NO_DISC */
static void copy_from(u32 type, uintptr_t mram, uintptr_t aram, u32 length, u32 disc) {
    aram_map();
    if (aram + length > ARAM_SIZE) {
        xhw_logf("[AR] DMA out of range: %08x + %u", (unsigned)aram, length);
        return;
    }
    xhw_commit((const void*)mram, length);
    xhw_mutex_lock(s_lock);
    if (type == 0) {
#ifdef XSDK_ARAM_VERIFY
        {
            static u8 tmp[0x80000];
            static u32 nchk, nbad;
            if (disc != NO_DISC && length <= sizeof tmp) {
                nchk++;
                if (!xsdk_dvd_image_read(disc, tmp, length) || memcmp(tmp, (const void*)mram, length)) nbad++;
                if ((nchk & 63) == 1 || nbad == 1) xhw_logf("[AR] verify: %u disc copies checked, %u differ", nchk, nbad);
            }
        }
#endif
        aram_write((u32)aram, (const u8*)mram, length, disc);
    } else {
        aram_read((u32)aram, (u8*)mram, length);
    }
    xhw_mutex_unlock(s_lock);
}

static u32 disc_source(u32 type, uintptr_t mram, u32 length) {
    u32 disc;
    if (type != 0 || !xsdk_dvd_disc_source((const void*)mram, length, &disc)) return NO_DISC;
    return disc;
}

static void arq_drain(void);

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length) {
    arq_drain();
    copy_from(type, (uintptr_t)mainmem_addr, aram_addr, length, disc_source(type, mainmem_addr, length));
    if (s_dma_cb) s_dma_cb(0);
}

u16 __ARGetInterruptStatus(void) { return 0; }
void __ARClearInterrupt(void) {}

/* ---- ARQ ----
 * Requests with a callback are copied by a worker thread, in the order
 * posted (one FIFO, both priorities: stricter than the GameCube's), and
 * their callbacks delivered on the game thread as before (ARQPoll, the
 * frame boundary, OSYieldThread, interrupts re-enabled). A copy out of
 * ARAM whose pages live on the disc image or in the swap file reads the
 * drive: done at post time on the game thread, round hw17's gold console
 * (a stock drive, its partition nearly full) froze ~6 s at room loads and
 * stalled past a second ~18 times a scene in CDvdFile::AsyncSeekRead ->
 * CARAMManager::DMAToMRAM. On the GameCube those copies run in the
 * background too. Requests without a callback wait for the queue, then
 * copy at once. The worker never takes the interrupt lock (a poster may
 * hold it while it waits for the queue): its queues have s_jlock.
 * (A first look blamed it for a title-screen Port Options frame without
 * text: the script's presses had landed elsewhere, xemu's timing drifting
 * with the screenshots; in lockstep both ways draw the same.)
 * -DXSDK_ARQ_ASYNC=0, or test builds' env PX_ARQ_ASYNC=0, copy at post
 * time. The worker runs either way: the reclaimer's swap-file writes go
 * there (aram_reclaim). */
#ifndef XSDK_ARQ_ASYNC
#define XSDK_ARQ_ASYNC 1
#endif
#define QMAX 512   /* copies waiting for the worker; more: at post time, after them */
typedef struct {
    ARQRequest* r;   /* NULL once removed (ARQRemoveRequest): copied, no callback */
    u32 type, length, disc;
    uintptr_t mram, aram;
} ArqJob;
static ArqJob s_job[QMAX];
static int s_job_head, s_job_n;
static ARQRequest* s_job_cur;      /* being copied */
static int s_job_cur_removed;
static volatile int s_job_busy;    /* posted, not yet copied */
/* copied, callbacks not yet run: a list through the requests' own next
 * (no bound: a room load posts hundreds of CDvdFile reads at once, and the
 * GameCube's queue has no limit either; a 128-entry array overflowed in
 * xemu with slowed image reads) */
static ARQRequest *s_done_first, *s_done_last;
static int s_done_n;
static volatile u32 s_reclaim_ask;   /* KB aram_reclaim left to the worker's swap-outs */
static u32 s_st_reclaim_kb;
static xhw_mutex* s_jlock;         /* s_job*, s_done* */
static xhw_event* s_job_event;
static int s_arq_init, s_async = -1, s_worker;
static u32 s_st_jobs, s_st_job_max, s_st_sync;

/* s_jlock held */
static void done_push(ARQRequest* r) {
    r->next = NULL;
    if (s_done_last) s_done_last->next = r;
    else s_done_first = r;
    s_done_last = r;
    s_done_n++;
}

static void arq_worker(void* arg) {
    (void)arg;
    for (;;) {
        ArqJob j;
        xhw_mutex_lock(s_jlock);
        while (s_job_n == 0) {
            const u32 ask = __atomic_exchange_n(&s_reclaim_ask, 0, __ATOMIC_SEQ_CST);
            xhw_mutex_unlock(s_jlock);
            if (ask) {   /* between copies: the cold chunks through the swap file */
                xhw_mutex_lock(s_lock);
                s_st_reclaim_kb += reclaim_passes(ask, 0, 3);
                xhw_mutex_unlock(s_lock);
            } else {
                xhw_event_wait(s_job_event, 100);
            }
            xhw_mutex_lock(s_jlock);
        }
        j = s_job[s_job_head];
        s_job_head = (s_job_head + 1) % QMAX;
        s_job_n--;
        s_job_cur = j.r;
        s_job_cur_removed = 0;
        xhw_mutex_unlock(s_jlock);
        copy_from(j.type, j.mram, j.aram, j.length, j.disc);
        xhw_mutex_lock(s_jlock);
        if (j.r && !s_job_cur_removed) done_push(j.r);
        s_job_cur = NULL;
        __atomic_fetch_sub(&s_job_busy, 1, __ATOMIC_SEQ_CST);
        xhw_mutex_unlock(s_jlock);
    }
}

static void arq_start(void) {
    if (s_jlock) return;
    s_jlock = xhw_mutex_create();
    s_job_event = xhw_event_create();
    {
        const char* e = XHW_TEST_BUILD ? getenv("PX_ARQ_ASYNC") : NULL;
        s_async = e ? atoi(e) != 0 : XSDK_ARQ_ASYNC;
    }
    s_worker = xhw_thread_start(arq_worker, NULL, 1, 64 * 1024);
    if (!s_worker) s_async = 0;
    xhw_logf("[AR] ARQ copies %s", s_async ? "on a worker thread" : "at post time");
}

static int arq_reclaim_ask(u32 kb) {
    if (!s_worker) return 0;
    __atomic_store_n(&s_reclaim_ask, kb, __ATOMIC_SEQ_CST);
    xhw_event_signal(s_job_event);
    return 1;
}

/* until every posted copy is done (the worker never needs the interrupt
 * lock, so the caller may hold it) */
static void arq_drain(void) {
    while (s_job_busy) xhw_sleep_ms(1);
}

void ARQInit(void) {
    arq_start();
    s_arq_init = 1;
}
void ARQReset(void) {}
BOOL ARQCheckInit(void) { return s_arq_init; }
void ARQSetChunkSize(u32 size) { (void)size; }
u32 ARQGetChunkSize(void) { return 4096; }
int aurora_arq_inflight(void) { return s_done_n + (int)s_job_busy; }

static void xsdk_arq_log(void) {
    if (s_st_jobs || s_st_sync || s_st_reclaim_kb)
        xhw_logf("[AR] ARQ: %u copies on the worker (at most %u waiting), %u at post time; %u KB reclaimed there; "
                 "now %d queued, %d not copied, %d callbacks waiting",
                 (unsigned)s_st_jobs, (unsigned)s_st_job_max, (unsigned)s_st_sync, (unsigned)s_st_reclaim_kb, s_job_n,
                 (int)s_job_busy, s_done_n);
    s_st_jobs = s_st_job_max = s_st_sync = s_st_reclaim_kb = 0;
}

void ARQPostRequest(ARQRequest* r, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
                    u32 length, ARQCallback callback) {
    const uintptr_t mram = type == 0 ? source : dest, aram = type == 0 ? dest : source;
    const u32 disc = disc_source(type, mram, length);
    r->next = NULL;
    r->owner = owner;
    r->type = type;
    r->priority = priority;
    r->source = source;
    r->dest = dest;
    r->length = length;
    r->callback = callback;
    if (aram + length > ARAM_SIZE)
        xhw_logf("[AR] ARQ type %u %08x -> %08x + %u from %p", (unsigned)type, (unsigned)source, (unsigned)dest,
                 (unsigned)length, __builtin_return_address(0));
    arq_start();
    if (s_async && callback) {
        xhw_mutex_lock(s_jlock);
        if (s_job_n < QMAX) {
            ArqJob* j = &s_job[(s_job_head + s_job_n++) % QMAX];
            j->r = r;
            j->type = type;
            j->mram = mram;
            j->aram = aram;
            j->length = length;
            j->disc = disc;
            __atomic_fetch_add(&s_job_busy, 1, __ATOMIC_SEQ_CST);
            s_st_jobs++;
            if ((u32)s_job_n > s_st_job_max) s_st_job_max = (u32)s_job_n;
            xhw_mutex_unlock(s_jlock);
            xhw_event_signal(s_job_event);
            return;
        }
        xhw_mutex_unlock(s_jlock);   /* full: at post time, after the rest */
    }
    arq_drain();
    copy_from(type, mram, aram, length, disc);
    s_st_sync++;
    if (!callback) return;
    xhw_mutex_lock(s_jlock);
    done_push(r);
    xhw_mutex_unlock(s_jlock);
}

/* the request's callback won't run; its copy still happens (as before,
 * when every copy was done as it was posted) */
static void arq_remove(ARQRequest* r, int by_owner, u32 owner) {
    int i;
    if (!s_jlock) return;
    xhw_mutex_lock(s_jlock);
    {   /* out of the list */
        ARQRequest *d = s_done_first, *prev = NULL;
        while (d) {
            ARQRequest* next = d->next;
            if (by_owner ? d->owner == owner : d == r) {
                if (prev) prev->next = next;
                else s_done_first = next;
                if (s_done_last == d) s_done_last = prev;
                s_done_n--;
            } else {
                prev = d;
            }
            d = next;
        }
    }
    for (i = 0; i < s_job_n; i++) {
        ArqJob* j = &s_job[(s_job_head + i) % QMAX];
        if (j->r && (by_owner ? j->r->owner == owner : j->r == r)) j->r = NULL;
    }
    if (s_job_cur && (by_owner ? s_job_cur->owner == owner : s_job_cur == r)) s_job_cur_removed = 1;
    xhw_mutex_unlock(s_jlock);
}

void ARQRemoveRequest(ARQRequest* r) { arq_remove(r, 0, 0); }
void ARQRemoveOwnerRequest(u32 owner) { arq_remove(NULL, 1, owner); }

void ARQFlushQueue(void) { xsdk_arq_deliver(); }

/* Callbacks run on the game thread with interrupts disabled, as in the
 * interrupt handler (Prime's take the game's own locks). */
void xsdk_arq_deliver(void) {
    BOOL intr;
    if (!s_jlock) return;
    intr = OSDisableInterrupts();
    for (;;) {
        ARQRequest* r;
        xhw_mutex_lock(s_jlock);
        r = s_done_first;
        if (!r) {
            xhw_mutex_unlock(s_jlock);
            break;
        }
        s_done_first = r->next;
        if (!s_done_first) s_done_last = NULL;
        s_done_n--;
        xhw_mutex_unlock(s_jlock);
        if (r && r->callback) r->callback(r);
    }
    OSRestoreInterrupts(intr);
}
