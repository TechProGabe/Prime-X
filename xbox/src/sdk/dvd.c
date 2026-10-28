/* dvd.c - the Dolphin DVD API over the user's disc image (.iso/.gcm/.ciso).
 *
 * The FST is read from the image at boot. Async reads run on one worker
 * thread and complete like the GameCube's DVD interrupt did: the callback
 * runs with interrupts disabled (the recursive OS lock), from that thread.
 * Synchronous reads take the same image lock on the calling thread. */
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <aurora/dvd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xsdk.h"

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* ======================================================================
 * Image reader
 * ====================================================================== */
static void* s_img;   /* xhw_file_open */
static uint64_t s_img_size, s_img_written;   /* for the disc check (and its cache) */
static xhw_mutex* s_img_lock;
/* CISO: block map, present blocks stored in order after 0x8000 */
static u32 s_ciso_block;
static u32* s_ciso_index;   /* block -> file offset, 0xFFFFFFFF = zero block */
static u32 s_ciso_blocks;

static int img_read_raw(u32 off, void* dst, u32 len) { return xhw_file_read(s_img, off, dst, len); }

/* into memory that is committed already */
static int img_read_committed(u32 off, void* dst, u32 len) {
    u8* d = (u8*)dst;
    int ok = 1;
    xhw_mutex_lock(s_img_lock);
    if (!s_ciso_index) {
        ok = img_read_raw(off, d, len);
    } else {
        while (len && ok) {
            u32 blk = off / s_ciso_block, in = off % s_ciso_block;
            u32 n = s_ciso_block - in;
            if (n > len) n = len;
            if (blk >= s_ciso_blocks || s_ciso_index[blk] == 0xFFFFFFFFu) memset(d, 0, n);
            else ok = img_read_raw(s_ciso_index[blk] + in, d, n);
            off += n;
            d += n;
            len -= n;
        }
    }
    xhw_mutex_unlock(s_img_lock);
    return ok;
}

static int img_read(u32 off, void* dst, u32 len) {
    xhw_commit(dst, len);   /* the kernel writes it: a lazy MEM1 chunk must exist first */
    return img_read_committed(off, dst, len);
}

static int img_open(const char* path) {
    u8 hdr[8];
    s_img = xhw_file_open(path);
    if (!s_img) return 0;
    xhw_file_stamp(s_img, &s_img_size, &s_img_written);
    if (!s_img_lock) s_img_lock = xhw_mutex_create();
    if (!img_read_raw(0, hdr, sizeof hdr)) return 0;
    if (memcmp(hdr, "CISO", 4) == 0) {
        u8* map = (u8*)malloc(0x8000 - 8);
        u32 i, pos = 0x8000;
        s_ciso_block = (u32)hdr[4] | (u32)hdr[5] << 8 | (u32)hdr[6] << 16 | (u32)hdr[7] << 24;
        s_ciso_blocks = 0x8000 - 8;
        s_ciso_index = (u32*)malloc(s_ciso_blocks * sizeof(u32));
        if (!map || !s_ciso_index || !s_ciso_block || !img_read_raw(8, map, 0x8000 - 8)) return 0;
        for (i = 0; i < s_ciso_blocks; i++) {
            s_ciso_index[i] = map[i] ? pos : 0xFFFFFFFFu;
            if (map[i]) pos += s_ciso_block;
        }
        free(map);
        xhw_logf("[DVD] CISO, %u KB blocks", s_ciso_block / 1024);
    }
    return 1;
}

/* ======================================================================
 * FST
 * ====================================================================== */
static DVDDiskID s_disk_id;
static u8* s_fst;          /* big-endian, as on the disc */
static u32 s_fst_entries;
static const char* s_fst_names;
static char s_locale_ext[4];

static int fst_is_dir(u32 i) { return s_fst[i * 12] != 0; }
static const char* fst_name(u32 i) { return s_fst_names + (be32(s_fst + i * 12) & 0x00FFFFFF); }
static u32 fst_w1(u32 i) { return be32(s_fst + i * 12 + 4); }   /* file: offset; dir: parent */
static u32 fst_w2(u32 i) { return be32(s_fst + i * 12 + 8); }   /* file: length; dir: next */

void aurora_dvd_set_locale_extension(const char* ext) {
    if (ext) snprintf(s_locale_ext, sizeof s_locale_ext, "%s", ext);
    else s_locale_ext[0] = '\0';
}

static int name_eq(const char* a, const char* b, size_t blen) {
    size_t i;
    for (i = 0; i < blen; i++)
        if (!a[i] || tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return a[blen] == '\0';
}

static s32 lookup(const char* path) {
    u32 dir = 0;
    while (*path == '/') path++;
    while (*path) {
        const char* end = strchr(path, '/');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        u32 i = dir + 1, stop = fst_w2(dir);
        int found = 0;
        while (i < stop) {
            if (name_eq(fst_name(i), path, len)) {
                found = 1;
                break;
            }
            i = fst_is_dir(i) ? fst_w2(i) : i + 1;
        }
        if (!found) return -1;
        if (!end) return (s32)i;
        if (!fst_is_dir(i)) return -1;
        dir = i;
        path = end + 1;
        while (*path == '/') path++;
    }
    return (s32)dir;
}

s32 DVDConvertPathToEntrynum(const char* path) {
    s32 e;
    if (!s_fst || !path) return -1;
    e = lookup(path);
    if (e < 0 && s_locale_ext[0]) {
        size_t n = strlen(path);
        if (n > 4 && (strcmp(path + n - 4, ".usd") == 0 || strcmp(path + n - 4, ".dat") == 0)) {
            char alt[256];
            snprintf(alt, sizeof alt, "%.*s.%s", (int)(n - 4), path, s_locale_ext);
            e = lookup(alt);
        }
    }
    return e;
}

BOOL DVDConvertEntrynumToPath(s32 entrynum, char* path, u32 maxlen) {
    if (!s_fst || entrynum < 0 || (u32)entrynum >= s_fst_entries || !maxlen) return FALSE;
    snprintf(path, maxlen, "%s", fst_name((u32)entrynum));
    return TRUE;
}

/* ======================================================================
 * Open / read
 * ====================================================================== */
BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fi) {
    if (!s_fst || entrynum < 0 || (u32)entrynum >= s_fst_entries || fst_is_dir((u32)entrynum)) return FALSE;
    memset(fi, 0, sizeof *fi);
    fi->startAddr = fst_w1((u32)entrynum);
    fi->length = fst_w2((u32)entrynum);
    fi->cb.state = DVD_STATE_END;
    return TRUE;
}

BOOL DVDOpen(const char* name, DVDFileInfo* fi) {
    s32 e = DVDConvertPathToEntrynum(name);
    if (e < 0) {
        xhw_logf("[DVD] not on the disc: %s", name);
        return FALSE;
    }
    return DVDFastOpen(e, fi);
}

BOOL DVDClose(DVDFileInfo* fi) {
    (void)fi;
    return TRUE;
}

static s32 read_now(DVDFileInfo* fi, void* addr, s32 length, s32 offset) {
    u32 len = (u32)length;
    if (offset < 0 || (u32)offset >= fi->length) return 0;
    if ((u32)offset + len > fi->length) len = fi->length - (u32)offset;
    if (!img_read(fi->startAddr + (u32)offset, addr, len)) return DVD_RESULT_FATAL_ERROR;
    return (s32)len;
}

s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio) {
    s32 r;
    (void)prio;
    fi->cb.state = DVD_STATE_BUSY;
    r = read_now(fi, addr, length, offset);
    fi->cb.transferredSize = r > 0 ? (u32)r : 0;
    fi->cb.state = r < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
    return r;
}

/* ---- async queue (one worker, FIFO) ---- */
typedef struct Req {
    DVDFileInfo* fi;          /* file read (DVDReadAsyncPrio) */
    DVDCommandBlock* block;   /* or absolute read */
    void* addr;
    s32 length, offset;
    DVDCallback fcb;
    DVDCBCallback bcb;
} Req;

/* The read whose completion callback is running: lets ar.c leave ARAM
 * copies of it on the disc (Prime's CDvdFile posts the buffer -> ARAM
 * transfer of an ARAM-cached file from that callback). */
static struct {
    const u8* addr;
    u32 len, image_off;
} s_done_read;
static u32 s_worker_tls;

int xsdk_dvd_disc_source(const void* p, u32 len, u32* image_off) {
    const u8* a = (const u8*)p;
    if (!s_done_read.addr) return 0;
    if (a < s_done_read.addr || len > s_done_read.len || (u32)(a - s_done_read.addr) > s_done_read.len - len) return 0;
    *image_off = s_done_read.image_off + (u32)(a - s_done_read.addr);
    return 1;
}

/* ar.c fills ARAM chunks with it from inside their commit: committing here
 * again would recurse */
int xsdk_dvd_image_read(u32 image_off, void* dst, u32 len) { return img_read_committed(image_off, dst, len); }

#define QUEUE 64
static Req s_q[QUEUE];
static volatile int s_qhead, s_qcount;
/* reads done, waiting for the game thread to finish them */
typedef struct {
    Req r;
    s32 res;
} Done;
static Done s_d[QUEUE];
static volatile int s_dhead, s_dcount;
void xsdk_dvd_deliver(void);
static xhw_mutex* s_qlock;
static xhw_event* s_qevent;
static volatile int s_inflight;

static void worker(void* arg) {
    (void)arg;
    xhw_tls_set(s_worker_tls, (void*)1);
    for (;;) {
        Req r;
        s32 res;
        xhw_mutex_lock(s_qlock);
        if (s_qcount == 0) {
            xhw_mutex_unlock(s_qlock);
            xhw_event_wait(s_qevent, 100);
            continue;
        }
        r = s_q[s_qhead];
        xhw_mutex_unlock(s_qlock);

        if (r.fi) {
            res = read_now(r.fi, r.addr, r.length, r.offset);
        } else {
            res = img_read((u32)r.offset, r.addr, (u32)r.length) ? r.length : DVD_RESULT_FATAL_ERROR;
        }
        /* the game thread finishes it (xsdk_dvd_deliver): Prime's callbacks
         * take the game's own locks, which deadlocks from this thread */
        xhw_mutex_lock(s_qlock);
        s_qhead = (s_qhead + 1) % QUEUE;
        s_qcount--;
        if (s_dcount == QUEUE) OSPanic(__FILE__, __LINE__, "DVD completion queue overflow");
        s_d[(s_dhead + s_dcount++) % QUEUE] = (Done){ r, res };
        xhw_mutex_unlock(s_qlock);
    }
}

static BOOL enqueue(const Req* r) {
    int slot;
    DVDCommandBlock* cb = r->fi ? &r->fi->cb : r->block;
    /* reads waiting and reads done but not yet delivered share the bound:
     * a full queue is drained here, on the game thread, as the next
     * interrupt would have */
    for (;;) {
        xhw_mutex_lock(s_qlock);
        if (s_qcount + s_dcount < QUEUE) break;
        xhw_mutex_unlock(s_qlock);
        xsdk_dvd_deliver();
        xhw_sleep_ms(1);
    }
    cb->state = DVD_STATE_BUSY;
    cb->addr = r->addr;
    cb->length = (u32)r->length;
    cb->offset = (u32)r->offset;
    slot = (s_qhead + s_qcount) % QUEUE;
    s_q[slot] = *r;
    s_qcount++;
    __atomic_add_fetch(&s_inflight, 1, __ATOMIC_RELEASE);
    xhw_mutex_unlock(s_qlock);
    xhw_event_signal(s_qevent);
    return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
    Req r;
    (void)prio;
    memset(&r, 0, sizeof r);
    r.fi = fi;
    r.addr = addr;
    r.length = length;
    r.offset = offset;
    r.fcb = callback;
    fi->callback = callback;
    return enqueue(&r);
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback,
                        s32 prio) {
    Req r;
    (void)prio;
    memset(&r, 0, sizeof r);
    r.block = block;
    r.addr = addr;
    r.length = length;
    r.offset = offset;
    r.bcb = callback;
    return enqueue(&r);
}

int aurora_dvd_inflight(void) { return __atomic_load_n(&s_inflight, __ATOMIC_ACQUIRE); }

/* On the game thread (the frame boundary, OSYieldThread, the alarm wait):
 * each finished read's state and callback together, with interrupts off,
 * as the DVD interrupt did. */
void xsdk_dvd_deliver(void) {
    if (!s_qlock || !__atomic_load_n(&s_dcount, __ATOMIC_ACQUIRE)) return;
    for (;;) {
        Done d;
        DVDCommandBlock* cb;
        BOOL intr;
        xhw_mutex_lock(s_qlock);
        if (s_dcount == 0) {
            xhw_mutex_unlock(s_qlock);
            return;
        }
        d = s_d[s_dhead];
        s_dhead = (s_dhead + 1) % QUEUE;
        s_dcount--;
        xhw_mutex_unlock(s_qlock);
        cb = d.r.fi ? &d.r.fi->cb : d.r.block;
        intr = OSDisableInterrupts();
        cb->transferredSize = d.res > 0 ? (u32)d.res : 0;
        cb->state = d.res < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
        if (d.res > 0) {
            s_done_read.addr = (const u8*)d.r.addr;
            s_done_read.len = (u32)d.res;
            s_done_read.image_off = d.r.fi ? d.r.fi->startAddr + (u32)d.r.offset : (u32)d.r.offset;
        }
        if (d.r.fi && d.r.fcb) d.r.fcb(d.res, d.r.fi);
        else if (!d.r.fi && d.r.bcb) d.r.bcb(d.res, d.r.block);
        s_done_read.addr = NULL;
        OSRestoreInterrupts(intr);
        __atomic_sub_fetch(&s_inflight, 1, __ATOMIC_RELEASE);
    }
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) { return block->state; }
s32 DVDGetFileInfoStatus(const DVDFileInfo* fi) { return fi->cb.state; }
s32 DVDGetTransferredSize(DVDFileInfo* fi) { return (s32)fi->cb.transferredSize; }
s32 DVDGetDriveStatus(void) { return aurora_dvd_inflight() ? DVD_STATE_BUSY : DVD_STATE_END; }
BOOL DVDCheckDisk(void) { return s_img != NULL; }
DVDDiskID* DVDGetCurrentDiskID(void) { return &s_disk_id; }
BOOL DVDSetAutoInvalidation(BOOL on) { (void)on; return TRUE; }
int DVDSetAutoFatalMessaging(BOOL on) { (void)on; return 0; }
void DVDPause(void) {}
void DVDResume(void) {}
void DVDReset(void) {}
int DVDResetRequired(void) { return 0; }
void* DVDGetFSTLocation(void) { return s_fst; }

s32 DVDCancel(volatile DVDCommandBlock* block) { (void)block; return 0; }
int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback cb) {
    if (cb) cb(0, block);
    return 1;
}

/* ---- main.dol, for the default font lifted out of it (PortFindDolResource) ---- */
static u8* s_dol;
static s32 s_dol_size;

const u8* DVDGetDOLLocation(s32* out_size) {
    if (!s_dol && s_img) {
        u8 hdr[0x440], dh[0x100];
        u32 dol, end = 0, i;
        /* the DOL's own header gives its size (Prime's FST is before it on the disc) */
        if (img_read(0, hdr, sizeof hdr) && img_read(dol = be32(hdr + 0x420), dh, sizeof dh)) {
            for (i = 0; i < 18; i++) {
                u32 off = be32(dh + i * 4), size = be32(dh + 0x90 + i * 4);
                if (size && off + size > end) end = off + size;
            }
            if (end >= sizeof dh && end <= (8u << 20) && (s_dol = (u8*)malloc(end)) != NULL) {
                s_dol_size = (s32)end;
                if (!img_read(dol, s_dol, end)) {
                    free(s_dol);
                    s_dol = NULL;
                }
            }
        }
    }
    if (out_size) *out_size = s_dol ? s_dol_size : 0;
    return s_dol;
}

void xsdk_dvd_free_dol(void) {
    free(s_dol);
    s_dol = NULL;
}

/* ======================================================================
 * Boot
 * ====================================================================== */
/* ======================================================================
 * Disc validation (docs/boot-safety.md): what a clean USA 1.00 image
 * holds is in disc_table.inc (tools/disc_file_table.py). Every boot: the
 * size (or a .ciso's block map), the FST and main.dol. First boot of an
 * image: every file's CRC (xsdk_disc_full_check), the result cached.
 * ====================================================================== */
typedef struct {
    const char* path;
    u32 off, size, crc;
} DiscFile;
typedef struct {
    const char* id;
    int rev;
    const char* name;
} DiscRelease;
#include "disc_table.inc"

static const char* disc_release_name(const char* id6, u32 rev) {
    u32 i;
    for (i = 0; i < sizeof k_disc_others / sizeof k_disc_others[0]; i++)
        if (memcmp(id6, k_disc_others[i].id, 6) == 0 && (u32)k_disc_others[i].rev == rev) return k_disc_others[i].name;
    return NULL;
}

/* bytes [off, off + len) are all stored (a .ciso drops unused blocks) */
static int img_has(u32 off, u32 len) {
    u32 b;
    if (!s_ciso_index || !len) return 1;
    for (b = off / s_ciso_block; b <= (off + len - 1) / s_ciso_block; b++)
        if (b >= s_ciso_blocks || s_ciso_index[b] == 0xFFFFFFFFu) return 0;
    return 1;
}

/* CRC-32 (zlib's polynomial), table-driven */
static u32 s_crc_table[256];
static u32 crc32_update(u32 crc, const u8* p, u32 n) {
    if (!s_crc_table[1]) {
        u32 i, k, c;
        for (i = 0; i < 256; i++) {
            for (c = i, k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            s_crc_table[i] = c;
        }
    }
    crc = ~crc;
    while (n--) crc = s_crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static u8* s_crc_buf;
/* bytes per read */
static u32 s_crc_chunk = 1u << 20;
static void disc_crc_buffer_free(void) {
    free(s_crc_buf);
    s_crc_buf = NULL;
}
/* CRC of [off, off + len) of the image; *ok = 0 on a read error */
static u32 img_crc(u32 off, u32 len, int* ok) {
    u32 crc = 0;
    *ok = 0;
    if (!s_crc_buf && !(s_crc_buf = (u8*)malloc(1u << 20))) return 0;
    while (len) {
        u32 n = len > s_crc_chunk ? s_crc_chunk : len;
        if (!img_read_committed(off, s_crc_buf, n)) return 0;
        crc = crc32_update(crc, s_crc_buf, n);
        off += n;
        len -= n;
    }
    *ok = 1;
    return crc;
}



static int disc_quick_check(u32 fst_len, char* why, size_t why_cap) {
    static const char k_changed[] =
        "This image has been changed or trimmed (NKit, a mod, a scrubbed copy). Prime-X needs a clean 1:1 dump.";
    u32 i, cut = 0, crc;
    int ok;
    if (!s_ciso_index && s_img_size != DISC_IMAGE_SIZE) {
        xhw_logf("[DISC] size %llu, a clean image is %u", (unsigned long long)s_img_size, DISC_IMAGE_SIZE);
        snprintf(why, why_cap, "%s", k_changed);
        return 0;
    }
    if (s_fst_entries != DISC_FST_ENTRIES || fst_len != DISC_FST_SIZE ||
        crc32_update(0, s_fst, fst_len) != DISC_FST_CRC) {
        xhw_logf("[DISC] file table differs (%u entries, %u bytes)", s_fst_entries, fst_len);
        snprintf(why, why_cap, "%s", k_changed);
        return 0;
    }
    for (i = 0; i < DISC_FILE_COUNT; i++)
        if (!img_has(k_disc_files[i].off, k_disc_files[i].size)) {
            if (!cut) xhw_logf("[DISC] cut short: %s", k_disc_files[i].path);
            cut++;
        }
    if (cut) {
        snprintf(why, why_cap, "This image is missing data (%u files cut short). Prime-X needs a clean dump.", cut);
        return 0;
    }
    crc = img_crc(DISC_DOL_OFFSET, DISC_DOL_SIZE, &ok);
    if (!ok) {
        snprintf(why, why_cap, "The image couldn't be read at offset %u (hard drive or disc fault).", DISC_DOL_OFFSET);
        return 0;
    }
    if (crc != DISC_DOL_CRC) {
        xhw_logf("[DISC] main.dol CRC %08x, a clean image has %08x", crc, DISC_DOL_CRC);
        snprintf(why, why_cap, "%s", k_changed);
        return 0;
    }
    xhw_logf("[DISC] quick check passed: %s, FST and main.dol match USA 1.00",
             s_ciso_index ? "CISO block map covers every file" : "size");
    disc_crc_buffer_free();
    return 1;
}

/* Every file's CRC, on the first boot of an image (size and write time not
 * in disc.ini) or when force is set. Returns how many files differ, -1 on a
 * read error (its offset in *bad_off), -2 when progress (called after each
 * file with 0..1: the splash's bar) asked to stop. */
/* this image passed a full check before (disc.ini: its size and write time) */
static void disc_ini_line(char* ini, size_t icap, char* line, size_t lcap) {
    snprintf(ini, icap, "%sdisc.ini", xhw_save_dir());
    snprintf(line, lcap, "ok %llu %llu", (unsigned long long)s_img_size, (unsigned long long)s_img_written);
}

/* Options > Check Disc (Next Start): the next start checks every file */
void xsdk_disc_forget(void) {
    char ini[300], line[128];
    disc_ini_line(ini, sizeof ini, line, sizeof line);
    xhw_logf("[DISC] %s: the next start checks every file (Options)", remove(ini) == 0 ? "disc.ini removed" : "no disc.ini");
}

int xsdk_disc_passed_before(void) {
    char ini[300], line[128], have[128] = { 0 };
    FILE* f;
    int same;
    disc_ini_line(ini, sizeof ini, line, sizeof line);
    if ((f = fopen(ini, "r")) == NULL) return 0;
    same = fgets(have, sizeof have, f) && strncmp(have, line, strlen(line)) == 0;
    fclose(f);
    return same;
}

int xsdk_disc_full_check(int force, xsdk_disc_progress progress, char* first_bad, size_t cap, u32* bad_off) {
    char ini[300], line[128];
    FILE* f;
    u32 i, bad = 0;
    uint64_t done = 0, total = 0;
    disc_ini_line(ini, sizeof ini, line, sizeof line);
    if (!force && xsdk_disc_passed_before()) {
        xhw_logf("[DISC] full check: passed before for this image (disc.ini)");
        return -3;
    }
    for (i = 0; i < DISC_FILE_COUNT; i++) total += k_disc_files[i].size;
    xhw_logf("[DISC] full check: %u files, %llu MB", DISC_FILE_COUNT, (unsigned long long)(total >> 20));
    for (i = 0; i < DISC_FILE_COUNT; i++) {
        const DiscFile* d = &k_disc_files[i];
        int ok;
        u32 crc = img_crc(d->off, d->size, &ok);
        if (!ok) {
            disc_crc_buffer_free();
            *bad_off = d->off;
            xhw_logf("[DISC] read error in %s at %u", d->path, d->off);
            return -1;
        }
        if (crc != d->crc) {
            if (!bad) snprintf(first_bad, cap, "%s", d->path);
            xhw_logf("[DISC] %s: CRC %08x, a clean image has %08x", d->path, crc, d->crc);
            bad++;
        }
        done += d->size;
        if (progress && progress((float)((double)done / (double)total), d->path, crc, crc == d->crc, s_crc_buf,
                                 d->off)) {
            disc_crc_buffer_free();
            xhw_logf("[DISC] full check skipped at %u of %u files (checks again next boot)", i + 1, DISC_FILE_COUNT);
            return -2;
        }
    }
    disc_crc_buffer_free();
    if (bad) {
        xhw_logf("[DISC] full check: %u files differ", bad);
    } else {
        if ((f = fopen(ini, "w")) != NULL) {
            fprintf(f, "%s\n; Prime-X: this image passed the full disc check (size, write time)\n", line);
            fclose(f);
        }
        xhw_logf("[DISC] full check passed: every file matches USA 1.00%s",
                 s_ciso_index ? "" : " (Redump " DISC_IMAGE_SHA1 " by size and contents)");
    }
    return (int)bad;
}

int xsdk_dvd_open(const char* path, char* why, size_t why_cap) {
    u8 hdr[0x440];
    u32 fst_off, fst_len;
    if (!img_open(path)) {
        snprintf(why, why_cap, "The disc image could not be opened.");
        return 0;
    }
    if (!img_read(0, hdr, sizeof hdr)) {
        snprintf(why, why_cap, "The disc image is too short.");
        return 0;
    }
    /* The code is the 1.00 build (the decomp's VERSION 0): later revisions
     * moved data and changed scripts (docs/boot-safety.md). */
    if (memcmp(hdr, "GM8E01", 6) != 0 || hdr[7] != 0) {
        const char* name = disc_release_name((const char*)hdr, hdr[7]);
        if (name)
            snprintf(why, why_cap, "This is Metroid Prime %s. Prime-X needs USA 1.00 (GM8E01, revision 0).", name);
        else if (memcmp(hdr, "GM8E01", 6) == 0)
            snprintf(why, why_cap, "This is Metroid Prime revision %u. Prime-X needs USA 1.00.", hdr[7]);
        else
            snprintf(why, why_cap, "This is not Metroid Prime NTSC-U (GM8E01): the image says %.6s.", (const char*)hdr);
        xhw_logf("[DISC] refused: %.6s rev %u", (const char*)hdr, hdr[7]);
        return 0;
    }
    if (be32(hdr + 0x1C) != 0xC2339F3Du) {
        snprintf(why, why_cap, "The disc image's header is damaged (no GameCube magic).");
        return 0;
    }
    memcpy(&s_disk_id, hdr, sizeof s_disk_id);
    xsdk_fill_disc_id(hdr);
    fst_off = be32(hdr + 0x424);
    fst_len = be32(hdr + 0x428);
    if (!fst_len || fst_len > (4u << 20) || !(s_fst = (u8*)malloc(fst_len)) || !img_read(fst_off, s_fst, fst_len)) {
        snprintf(why, why_cap, "The disc's file table could not be read.");
        return 0;
    }
    s_fst_entries = fst_w2(0);
    s_fst_names = (const char*)s_fst + s_fst_entries * 12;
    xhw_logf("[DVD] GM8E01 rev %u, %u FST entries", hdr[7], s_fst_entries);
    return disc_quick_check(fst_len, why, why_cap);
}

void DVDInit(void) {
    static int started;
    if (started) return;
    started = 1;
    s_qlock = xhw_mutex_create();
    s_qevent = xhw_event_create();
    s_worker_tls = xhw_tls_alloc();
    xhw_thread_start(worker, NULL, 1, 64 * 1024);
}
