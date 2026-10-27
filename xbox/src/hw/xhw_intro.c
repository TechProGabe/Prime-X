/* xhw_intro.c - the boot screen's music: intro.pcm next to default.xbe,
 * played while the first boot's disc check runs (docs/ui.md "Boot
 * splash"; the user, 2026-10-27: "the first boot is a spectacle").
 *
 * As cheap as audio gets: the file is already what the AC97 path takes
 * (16-bit stereo at the game's 32 kHz, volume baked in at 75%,
 * tools/xbox/make_intro.py), so a thread only reads 32 KB at a time from
 * the disk into the ring xhw_audio.c plays from, and sleeps. The only
 * arithmetic is the one-second fade when the check ends. Loops if the
 * check outlasts the song. The game's own audio starts on the same ring at
 * the same rate after it (xhw_audio_init is a no-op then).
 *
 *   xhw_intro_start()   opens intro.pcm (none: silence) and starts playing
 *   xhw_intro_stop()    fades out over a second and stops; returns once done */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#define INTRO_RATE 32000
#define CHUNK_FRAMES 8192   /* 32 KB a read */
#define FADE_FRAMES INTRO_RATE

static void* s_file;
static uint32_t s_size, s_pos;
static int16_t* s_buf;
static volatile int s_run, s_stop, s_done;

static void intro_thread(void* arg) {
    uint32_t fade_left = FADE_FRAMES;
    (void)arg;
    while (s_run) {
        uint32_t space = xhw_audio_space(), n, k;
        if (space < CHUNK_FRAMES) {
            xhw_sleep_ms(30);
            continue;
        }
        n = CHUNK_FRAMES;
        if (s_pos + n * 4 > s_size) n = (s_size - s_pos) / 4;
        if (n == 0 || !xhw_file_read(s_file, s_pos, s_buf, n * 4)) {
            s_pos = 0;   /* the end, or a bad read: from the top */
            continue;
        }
        s_pos += n * 4;
        if (s_stop) {
            /* the fade: the rest of the second, linear */
            for (k = 0; k < n && fade_left; k++, fade_left--) {
                int32_t g = (int32_t)fade_left * 256 / FADE_FRAMES;
                s_buf[2 * k] = (int16_t)(s_buf[2 * k] * g >> 8);
                s_buf[2 * k + 1] = (int16_t)(s_buf[2 * k + 1] * g >> 8);
            }
            if (k < n) memset(s_buf + 2 * k, 0, (n - k) * 4);
            xhw_audio_write(s_buf, n);
            if (!fade_left) break;
        } else {
            xhw_audio_write(s_buf, n);
        }
    }
    s_done = 1;
}

void xhw_intro_start(void) {
    char path[300];
    uint64_t size = 0, written;
    if (s_run) return;
    snprintf(path, sizeof path, "%sintro.pcm", xhw_game_dir());
    s_file = xhw_file_open(path);
    if (!s_file || !xhw_file_stamp(s_file, &size, &written) || size < 4 * INTRO_RATE) {
        xhw_logf("[INTRO] no %s: the check runs silent", path);
        return;
    }
    s_size = (uint32_t)(size & ~3ull);
    s_buf = (int16_t*)malloc(CHUNK_FRAMES * 4);
    if (!s_buf || !xhw_audio_init(INTRO_RATE)) {
        xhw_logf("[INTRO] no audio: the check runs silent");
        return;
    }
    s_pos = 0;
    s_stop = 0;
    s_done = 0;
    s_run = 1;
    if (!xhw_thread_start(intro_thread, NULL, 0, 16 * 1024)) {
        s_run = 0;
        return;
    }
    xhw_logf("[INTRO] playing %s (%u s)", path, s_size / 4 / INTRO_RATE);
}

void xhw_intro_stop(void) {
    int i;
    if (!s_run) return;
    s_stop = 1;
    for (i = 0; i < 300 && !s_done; i++) xhw_sleep_ms(10);   /* the second's fade, then out */
    s_run = 0;
    for (i = 0; i < 50 && !s_done; i++) xhw_sleep_ms(10);
    free(s_buf);
    s_buf = NULL;
    xhw_logf("[INTRO] stopped");
}
