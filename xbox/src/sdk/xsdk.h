/* xsdk.h - shared between the Dolphin SDK files in xbox/src/sdk (game triple). */
#ifndef XSDK_H
#define XSDK_H
#include <dolphin/card.h>
#include <stdarg.h>
#include <stddef.h>

/* os.c */
void xsdk_os_wait_alarm(void);   /* sleep toward the next alarm */
void xsdk_os_yield(void);
void xsdk_log_line(const char* fmt, ...);
int xsdk_is_game_thread(void);
void xsdk_run_alarms(void);
int xsdk_lockstep(void);         /* env PX_LOCKSTEP=1: one simulation tick per frame (os.c) */
void xsdk_lockstep_advance(u32 ns);
void xsdk_fill_disc_id(const void* header32);
void xsdk_card_dispatch(CARDCallback callback, s32 chan, s32 result);
void xsdk_log_raw(const char* text);   /* log.c: no newline added */
int xsdk_vsnprintf(char* out, size_t cap, const char* fmt, va_list ap);   /* log.c: vsnprintf with %f/%e/%g */
int xsdk_snprintf(char* out, size_t cap, const char* fmt, ...);

/* dvd.c */
int xsdk_dvd_open(const char* path, char* why, size_t why_cap);
/* every file's CRC on an image's first boot (docs/boot-safety.md): after each
 * file, progress gets 0..1, the file, its CRC, whether it matches, a few of
 * its bytes and its offset; a nonzero return stops the check (-2). -3: the
 * image passed before (disc.ini), nothing was read. */
typedef int (*xsdk_disc_progress)(float f, const char* file, u32 crc, int ok, const u8* sample, u32 off);
int xsdk_disc_full_check(int force, xsdk_disc_progress progress, char* first_bad, size_t cap, u32* bad_off);
void xsdk_dvd_deliver(void);
void xsdk_dvd_free_dol(void);
/* On the DVD worker, inside a read's completion callback: 1 if [p, p + len)
 * holds bytes that read just brought from the disc image, and where from. */
int xsdk_dvd_disc_source(const void* p, u32 len, u32* image_off);
int xsdk_dvd_image_read(u32 image_off, void* dst, u32 len);   /* dst committed; 1 on success */

/* ar.c */
void xsdk_arq_deliver(void);
void* xsdk_aram_base(void);
u32 xsdk_aram_size(void);
u32 xsdk_aram_disc_kb(void);   /* ARAM contents left on the disc image */

/* pad.c: a controller as PADRead last read it (NULL: not connected) */
struct xhw_pad;
const struct xhw_pad* xsdk_pad_raw(int port);

/* vi.c: frame boundary; the game side keeps the camera aspect current (port) */
void xsdk_widescreen_update(void);
void xsdk_frame_boundary(void);
void xsdk_vi_flip_now(void);   /* the retrace callbacks, now (prime_sdk.c) */
void xsdk_prime_sdk_init(void);

/* gx backend: end the frame, flip (black: VISetBlack) */
void xsdk_gx_present(int black);
unsigned xsdk_frame_count(void);

#endif
