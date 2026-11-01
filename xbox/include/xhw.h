/* xhw.h - the Xbox hardware layer (xbox/src/hw, nxdk's i386-pc-win32 triple)
 * as seen by the Dolphin SDK implementation (xbox/src/sdk, the game triple).
 *
 * Only scalars, pointers and structs without bit-fields or 64-bit members
 * cross this boundary, so both triples agree on every layout here. */
#ifndef XHW_H
#define XHW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test builds (the profiler's or autopad's, or -DXHW_TEST_BUILD=1) keep the
 * tools for console rounds: BACK takes a screenshot (BACK+Y flushes the
 * caches) and the frame-rate counter is on by default. A plain build is a
 * release: BACK does nothing and the counter is off unless settings.ini
 * says fps = 1. */
#ifndef XHW_TEST_BUILD
#if (defined(XHW_PROF) && XHW_PROF) || (defined(XHW_AUTOPAD) && XHW_AUTOPAD) || (defined(XHW_PMC) && XHW_PMC)
#define XHW_TEST_BUILD 1
#else
#define XHW_TEST_BUILD 0
#endif
#endif
/* -DXHW_LOG_ALWAYS=1: a release build (no test tools) whose boot.log is on
 * from boot whatever Port Options says: a tester's diagnostic build */
#ifndef XHW_LOG_ALWAYS
#define XHW_LOG_ALWAYS 0
#endif

/* ---- logging (COM1 when present, E:\UDATA\...\boot.log / last.log) ---- */
void xhw_log(const char* line);
void xhw_logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
/* Unrecoverable: draw the message on screen, log it, wait, go to the dashboard. */
void xhw_fatal(const char* title, const char* msg) __attribute__((noreturn));
/* a debug-screen page: the title and NULL-terminated lines (no wait) */
void xhw_error_screen(const char* title, const char* const* lines);
void xhw_choice_screen(const char* title, const char* const* lines);   /* no dashboard line: the caller asks */

/* ---- time ---- */
uint64_t xhw_ticks(void);            /* the kernel's counter (the ACPI timer) */
uint64_t xhw_ticks_per_sec(void);
uint64_t xhw_time_ns(void);          /* monotonic */
uint32_t xhw_tick_ms(void);          /* the kernel's 1 ms tick count */
/* 0 once rdtsc was found to trap (a BIOS patcher rewrote it): callers that
 * time themselves by the TSC use xhw_tick_ms instead */
int xhw_tsc_usable(void);
/* Once a presented frame (the frame boundary): counts VBlank against the
 * clock over the first seconds and scales the clock to the display when
 * the ACPI timer is off its rate (docs/boot-safety.md). real_time 0: skip. */
void xhw_clock_display_frame(int real_time);
/* Wall clock: seconds since 2000-01-01 00:00:00 local time, plus the
 * sub-second part in nanoseconds (the GameCube RTC epoch). */
int64_t xhw_wallclock_2000(uint32_t* ns_out);
void xhw_sleep_ms(uint32_t ms);
void xhw_yield(void);

/* ---- frame profile: a [PERF] line every 5 s (xhw_perf.c) ----
 * Game-thread time is charged to one bucket at a time; enter switches to a
 * bucket and returns the one to restore with leave, so nested sections are
 * charged exclusively. Everything not in a section is game logic. */
enum {
    XHW_PERF_LOGIC,   /* the game's simulation ticks (and anything preempting it) */
    XHW_PERF_RENDER,  /* the game's render pass: HSD walking the scene, GX state */
    XHW_PERF_DLIST,   /* GX display lists -> canonical vertices, and their draws' back end */
    XHW_PERF_DRAW,    /* back end of the immediate-mode draws: state diff, pushbuffer */
    XHW_PERF_TEX,     /* texture conversion and upload */
    XHW_PERF_EFB,     /* EFB copies read back on the CPU */
    XHW_PERF_GPU,     /* waiting for the GPU to go idle */
    XHW_PERF_VSYNC,   /* pacing to 60 Hz: spare time */
    XHW_PERF_N
};
#if !defined(XHW_PMC) || !XHW_PMC
/* inline: they run per display list and per draw, and as calls into the hw
 * library (outside ThinLTO) they were ~1.2% of the console's CPU (round
 * hw5); the probe build's PMC charging keeps the calls (xhw_perf.c) */
extern int xhw_perf_cur;
extern uint64_t xhw_perf_mark, xhw_perf_acc[XHW_PERF_N];
static inline void xhw_perf_charge_(void) {
    uint32_t lo, hi;
    uint64_t now;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    now = (uint64_t)hi << 32 | lo;
    if (xhw_perf_mark) xhw_perf_acc[xhw_perf_cur] += now - xhw_perf_mark;
    xhw_perf_mark = now;
}
static inline int xhw_perf_enter(int bucket) {
    int prev = xhw_perf_cur;
    xhw_perf_charge_();
    xhw_perf_cur = bucket;
    return prev;
}
static inline void xhw_perf_leave(int prev) {
    xhw_perf_charge_();
    xhw_perf_cur = prev;
}
#else
int xhw_perf_enter(int bucket);
void xhw_perf_leave(int prev);
#endif
uint64_t xhw_perf_now(void);                   /* rdtsc, for xhw_perf_audio */
void xhw_perf_audio(uint64_t ticks);           /* mixer thread: time spent mixing */
void xhw_perf_frame(uint32_t draws, uint32_t verts);   /* once per presented frame */
void xhw_perf_ticks(uint32_t n);               /* simulation ticks run before a render pass */
/* the last presented frame: microseconds in each XHW_PERF_* bucket, ticks run */
void xhw_perf_last_frame(uint32_t us[XHW_PERF_N], uint32_t* ticks);
int xhw_perf_bucket(void);                     /* the game thread's current bucket (the profiler's [PROFS]) */
/* the game's addition to each [PERF] line (port_debug.cpp: Game Timing:
 * Auto's share of frames at 30 Hz ticks); NULL: none */
extern const char* (*xhw_perf_tag)(void);
/* MXCSR flush-to-zero (+ DAZ where the CPU has it) on the calling thread:
 * the probe's ablation window 1 (xhw_pmc.c; xhw_ablate in xgx_probe.h) */
void xhw_set_ftz(int on);
/* -DXHW_PROF=1 (xhw_prof.c): the whole-match profile restarts at each scene's
 * entry and is written out once at the match's end ([PROFH], prof.bin);
 * no-ops in other builds. Game thread; the sampler does the work. */
void xhw_prof_scene_enter(void);
void xhw_prof_match_end(void);
/* The mixer thread (px_sdl3.cpp, on itself): a sample that finds it running
 * goes to the [PROFA] histogram instead of the game code it preempted. */
void xhw_prof_set_mixer_thread(void);
/* -DXHW_PGO=1 (xhw_pgo.c, an XBOX_PGO=gen build): the -fprofile-generate
 * counters so far as [PGOC] lines (tools/xbox/pgo_raw.py); no-op otherwise */
void xhw_pgo_dump(const char* why);                 /* from xhw_led_match_end: TIME!/GAME! */

/* ---- threads and locks ---- */
typedef struct xhw_mutex xhw_mutex;
xhw_mutex* xhw_mutex_create(void);   /* recursive */
void xhw_mutex_lock(xhw_mutex* m);
void xhw_mutex_unlock(xhw_mutex* m);
int xhw_mutex_trylock(xhw_mutex* m);   /* 1: locked (unlock it), 0: another thread holds it */
typedef struct xhw_event xhw_event;
xhw_event* xhw_event_create(void);   /* auto-reset */
void xhw_event_signal(xhw_event* e);
int xhw_event_wait(xhw_event* e, uint32_t timeout_ms);   /* 1 signalled, 0 timeout */
/* Thread-local slots (TlsAlloc): the game triple can't use __thread here. */
uint32_t xhw_tls_alloc(void);
void* xhw_tls_get(uint32_t slot);
void xhw_tls_set(uint32_t slot, void* value);
/* priority: -2 (lowest) .. +2 (highest), relative to the game thread (0) */
int xhw_thread_start(void (*fn)(void*), void* arg, int priority, uint32_t stack_bytes);

/* ---- memory ---- */
/* Reserve and commit `bytes` at exactly `va` (page aligned); NULL on failure. */
void* xhw_alloc_at(uintptr_t va, uint32_t bytes);
/* Reserve `bytes` at exactly `va` and commit it on demand, 64 KB at a time:
 * the first touch of a chunk faults and the thread's crash guard commits it
 * (zero-filled). Memory the kernel writes (file reads) and memory touched at
 * raised IRQL must be committed first with xhw_commit. NULL on failure. */
void* xhw_reserve_lazy(uintptr_t va, uint32_t bytes);
/* Commit [p, p + bytes) if it lies in a lazy region; no-op otherwise. */
void xhw_commit(const void* p, uint32_t bytes);
/* Crash guard hook: commits the chunk at `addr`; 1 if it did. */
int xhw_lazy_fault(uintptr_t addr);
uint32_t xhw_lazy_committed_kb(void);
void xhw_lazy_log_map(void);   /* [MEM] lazy: committed chunks per 4 MB range */
/* Chunks of a lazy region whose data can live elsewhere (disc-backed ARAM):
 * `fill` runs whenever a chunk gets committed (first touch, xhw_commit),
 * after the commit and before the chunk counts as committed, and puts its
 * data in. xhw_lazy_decommit gives a whole chunk's memory back. */
#define XHW_LAZY_CHUNK (64u * 1024)
void xhw_lazy_set_fill(const void* base, void (*fill)(void* chunk));
int xhw_lazy_is_committed(const void* p);
void xhw_lazy_decommit(void* chunk);
/* Commit a chunk's pages again, bookkeeping untouched: a fill that may have
 * waited on its owner's lock while that owner decommitted the chunk calls
 * it under the lock before writing (ar.c). 0: no memory for it. */
int xhw_lazy_commit_pages(void* chunk);
uint32_t xhw_mem_free_kb(void);
/* Memory pressure. A reclaimer gives memory back (decommits chunks whose
 * data can be had again, drops caches) and returns the KB it freed; asked
 * for `want_kb`. `urgent`: called from a failed commit, on any thread and
 * with any locks held: it must not block on a lock (try-lock and skip) and
 * must not malloc or free. xhw_mem_reclaim asks each in the order added.
 * xhw_mem_tick (the game thread, once a frame, no locks held) reclaims
 * while less than XHW_MEM_LOW_KB is free. */
typedef uint32_t (*xhw_reclaim_fn)(uint32_t want_kb, int urgent);
void xhw_mem_add_reclaim(xhw_reclaim_fn fn, int first);   /* first: asked before those already added */
uint32_t xhw_mem_reclaim(uint32_t want_kb, int urgent);
#ifndef XHW_MEM_LOW_KB
#define XHW_MEM_LOW_KB (3u * 1024)
#endif
void xhw_mem_tick(void);
void xhw_mem_log_reclaim(void);   /* [MEM] failed commits a reclaim rescued, if any */
/* Memory breakdown (-DXHW_MEMB=<secs>, docs/testing.md): _due is 1 once
 * every `secs` seconds; _log writes a [MEMB] line with the kernel's buckets,
 * the malloc heap and the thread count, then `extra` (the caller's own). */
#ifndef XHW_MEMB
#define XHW_MEMB (XHW_TEST_BUILD ? 60 : 0)
#endif
int xhw_mem_breakdown_due(uint32_t secs);
void xhw_mem_breakdown_log(const char* extra);
/* 128 MB consoles: takes the RAM above 64 MB for good, so the game and the
 * kernel run in the low 64 MB as on a stock console (every boot, xsdk_early).
 * Returns the KB held; 0 on a 64 MB console. */
uint32_t xhw_mem_hold_upper(void);
/* 1: the console has RAM above 64 MB (MmQueryStatistics' physical pages). */
int xhw_mem_has_upper(void);

/* ---- files and paths ---- */
/* The folder default.xbe runs from, mounted as D:\ ("D:\\"). */
const char* xhw_game_dir(void);
/* Writable data folder: "E:\\UDATA\\<title id>\\" (created at boot). */
const char* xhw_save_dir(void);
/* Directory helpers (FATX): mkdir ignores "already exists". */
int xhw_mkdir(const char* path);
/* Copies a file, replacing the destination; 0 on failure. */
int xhw_copy_file(const char* from, const char* to);
/* Renames `from` over `to` (same volume) and flushes the volume's directory
 * entries. LOST_TO: failed after `to` was deleted, `from` is still there. */
enum { XHW_REPLACE_FAILED = 0, XHW_REPLACE_OK = 1, XHW_REPLACE_LOST_TO = 2 };
int xhw_replace_file(const char* from, const char* to);
typedef struct xhw_dir_entry { char name[64]; int is_dir; uint32_t size; } xhw_dir_entry;
/* pattern like "E:\\dir\\*.gci"; returns a handle (NULL: nothing found).
 * xhw_dir_next returns 0 at the end and closes the handle. */
void* xhw_dir_first(const char* pattern, xhw_dir_entry* out);
int xhw_dir_next(void* handle, xhw_dir_entry* out);
/* Flush a stdio FILE's data and the volume's directory entry to disk. */
void xhw_flush(void* stdio_file);
/* The save folder's error when it can't be written (boot.log is then in
 * D:\ instead; xhw_sys.c), else 0. */
unsigned xhw_log_fallback(void);
/* nv2a.c: a notice, two lines at the top of the picture for 10 s (the audio
 * driver's stuck engine, a save or screenshot that couldn't be written); any
 * thread. A call while another notice is waiting or up is dropped. */
void xhw_notice(const char* line1, const char* line2);
/* Read-only file with positioned reads straight into the caller's buffer
 * (pdclib's fread goes 1 KB ReadFile at a time and copies byte by byte).
 * xhw_file_read returns 1 if all `len` bytes were read. */
void* xhw_file_open(const char* path);
int xhw_file_read(void* file, uint32_t off, void* dst, uint32_t len);
/* size in bytes and last-write time (100 ns units), for the disc check's cache */
int xhw_file_stamp(void* file, uint64_t* size, uint64_t* written);
/* Read-write scratch file, created empty (truncated if it was there);
 * xhw_file_write returns 1 if all `len` bytes were written. */
void* xhw_file_create_rw(const char* path);
int xhw_file_write(void* file, uint32_t off, const void* src, uint32_t len);
/* Read-write file kept as it was (created if missing) and grown to at
 * least min_bytes now, so later writes inside that never extend it (on a
 * nearly full FATX partition growing a file is the slow part). NULL if it
 * can't be opened; a size it can't reach is left as it is. */
void* xhw_file_open_rw_sized(const char* path, uint32_t min_bytes);

/* ---- controllers ---- */
typedef struct xhw_pad {
    int connected;
    uint32_t buttons;            /* XHW_BTN_* */
    int16_t lx, ly, rx, ry;      /* -32768..32767, y up */
    uint8_t lt, rt;              /* 0..255 */
} xhw_pad;
enum {
    XHW_BTN_A = 1u << 0, XHW_BTN_B = 1u << 1, XHW_BTN_X = 1u << 2, XHW_BTN_Y = 1u << 3,
    XHW_BTN_BLACK = 1u << 4, XHW_BTN_WHITE = 1u << 5, XHW_BTN_START = 1u << 6,
    XHW_BTN_BACK = 1u << 7, XHW_BTN_LSTICK = 1u << 8, XHW_BTN_RSTICK = 1u << 9,
    XHW_BTN_UP = 1u << 10, XHW_BTN_DOWN = 1u << 11, XHW_BTN_LEFT = 1u << 12,
    XHW_BTN_RIGHT = 1u << 13,
};
void xhw_pad_poll(void);                       /* once per PADRead */
void xhw_pad_set_shots(int on);                /* BACK screenshots (always on in test builds) */
int xhw_pad_get(int port, xhw_pad* out);       /* port 0..3; returns connected */
void xhw_pad_rumble(int port, uint16_t low, uint16_t high);
/* BACK+Y: drop every cached texture and display list at the next frame end
 * (a diagnostic: does a wrong surface come back right from fresh data?).
 * Returns and clears the request. */
int xhw_debug_flush_take(void);

/* ---- audio: 32 kHz stereo s16 pushed by the AX mixer ---- */
int xhw_audio_init(uint32_t rate);
/* Frames the output can take right now without blocking. */
uint32_t xhw_audio_space(void);
void xhw_audio_write(const int16_t* stereo, uint32_t frames);
void xhw_audio_stop(void);

/* ---- video ---- */
typedef struct xhw_video_mode {
    int width, height;           /* framebuffer: 640x480 or 1280x720 */
    int bpp;                     /* 32, or 16 at 720p */
    int widescreen;              /* 1: the picture is 16:9 */
    int progressive;             /* 480p/720p */
} xhw_video_mode;
const xhw_video_mode* xhw_video(void);
/* Dashboard settings: which modes the AV pack and the user allow. */
int xhw_video_720p_allowed(void);
int xhw_video_480p_allowed(void);
int xhw_video_widescreen_set(void);
/* Before boot (settings.ini): 1 uses 720p where the dashboard allows it
 * (experimental, off by default). */
void xhw_video_set_pref_720p(int on);
/* Before boot (settings.ini): 0 forces 480i where the dashboard allows 480p. */
void xhw_video_set_pref_480p(int on);
/* Picks 480/720 and sets the mode, before pbkit starts. Ends the splash. */
void xhw_video_boot(void);
void xhw_wait_vblank(void);
/* Boot title card (xhw_splash.c): load bar, 0..1; nothing once the mode is set. */
void xhw_splash_progress(float f);
/* the task line ("VERIFYING DISC IMAGE") and two note lines under the bar (NULL: none) */
void xhw_splash_stage(const char* task, const char* note1, const char* note2);
/* a line of the boot log (top left), typed on */
void xhw_splash_log(const char* line);
/* the detail line and the hex line under the bar (the disc check's file) */
void xhw_splash_detail(const char* line1, const char* line2);
/* B pressed on any controller (the first boot's disc check can be skipped) */
int xhw_splash_skip_pressed(void);
/* the boot screen's music (xhw_intro.c): intro.pcm next to default.xbe */
void xhw_intro_start(void);
void xhw_intro_stop(void);   /* a one-second fade, then returns */
/* default.xbe runs from the DVD drive (a burned disc; xemu) */
int xhw_running_from_dvd(void);
/* an error card in the splash's look; 0 if the splash no longer owns the screen */
int xhw_splash_card(const char* title, const char* const* lines);

/* settings.ini's log_file (Port Options > System > Log File): release
 * builds write boot.log only when it is on, from the moment it is set (the
 * lines since boot are kept in memory until settings.ini is read); test
 * builds always write it. Crash and hang reports are written either way. */
void xhw_log_set_file(int on);

/* ---- front LED effects (xhw_led.c) ----
 * Game events, posted from the game thread: a few stores and a SetEvent when
 * the effects are on, nothing when off. The SMBus writes happen on a worker. */
void xhw_led_enable(int on);                  /* settings.ini led; off hands the LED back */
void xhw_led_preview(void);                   /* the menu: a short sweep after turning it on */
void xhw_led_scene(void);                     /* a scene change: back to the SMC */
void xhw_led_ko(int port, int stocks_left);   /* stocks_left -1: not a stock match */
void xhw_led_timer(int seconds_left);         /* a timed match's countdown, once a second */
void xhw_led_match_end(int outcome);          /* GAME!/TIME!; 7 no contest */
void xhw_autopad_tick(uint32_t tick);         /* simhash.c: the match's tick, for an autopad TSHOT */

/* ---- system ---- */
void xhw_quit_to_dashboard(void) __attribute__((noreturn));
void xhw_reboot_self(void) __attribute__((noreturn));
/* Why the XBE is about to leave (before one of the two above): the next
 * boot logs it as "[BOOT] previous exit". Copied; the last call wins. */
void xhw_exit_reason(const char* why);

#ifdef __cplusplus
}
#endif
#endif
