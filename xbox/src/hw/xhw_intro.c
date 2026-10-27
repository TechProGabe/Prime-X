/* xhw_intro.c - the boot screen's music, played while the first boot's disc
 * check runs (docs/ui.md "Boot splash"; the user, 2026-10-27: "the first
 * boot is a spectacle"). It rides at the end of default.xbe, so a copy of
 * the game always has it (the user, round hw2); tools/xbox/make_intro.py
 * appends it and documents the layout: IMA ADPCM in QuickTime packets, 32
 * kHz stereo at the game's rate, volume baked in at 75%, and a 16-byte
 * footer ("PXIN", offset, length, version) ending the file.
 *
 * As cheap as audio gets: a thread reads 8.5 KB at a time from the disk,
 * expands it (a table lookup and a few adds a sample, ~64k samples a
 * second) into the ring xhw_audio.c plays from, and sleeps. The only other
 * arithmetic is the one-second fade when the check ends. Loops if the check
 * outlasts the song. The game's own audio starts on the same ring at the
 * same rate after it (xhw_audio_init is a no-op then).
 *
 *   xhw_intro_start()   finds the music (none: silence) and starts playing
 *   xhw_intro_stop()    fades out over a second and stops; returns once done */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#define INTRO_RATE 32000
#define PACKET_BYTES 68     /* left 34, right 34: a header and 64 nibbles each */
#define PACKET_FRAMES 64
#define CHUNK_PACKETS 128   /* 8704 bytes, 8192 frames a read */
#define CHUNK_FRAMES (CHUNK_PACKETS * PACKET_FRAMES)
#define FADE_FRAMES INTRO_RATE

static void* s_file;
static uint32_t s_off, s_size, s_pos;   /* the payload in default.xbe; s_pos within it */
static uint8_t* s_packets;
static int16_t* s_buf;
static volatile int s_run, s_stop, s_done;

/* ---- IMA ADPCM, QuickTime flavour (as ffmpeg's adpcm_ima_qt decodes it) ---- */
static const int16_t k_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const int8_t k_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int pred, index; } adpcm_ch;
static adpcm_ch s_ch[2];

/* one channel's 34 bytes into every other sample of out */
static void decode_channel(adpcm_ch* c, const uint8_t* p, int16_t* out) {
    int head = p[0] << 8 | p[1], pred = (int16_t)(head & 0xff80), index = head & 0x7f, m;
    if (index > 88) index = 88;
    /* the header holds 9 bits of the predictor: carry on from the last
     * packet unless it says something else (a loop, or the first packet) */
    if (c->index != index || pred - c->pred > 0x7f || c->pred - pred > 0x7f) {
        c->pred = pred;
        c->index = index;
    }
    pred = c->pred;
    index = c->index;
    for (m = 0; m < PACKET_FRAMES; m++) {
        int nib = m & 1 ? p[2 + (m >> 1)] >> 4 : p[2 + (m >> 1)] & 15;
        int step = k_step[index], diff = step >> 3;
        if (nib & 4) diff += step;
        if (nib & 2) diff += step >> 1;
        if (nib & 1) diff += step >> 2;
        pred = nib & 8 ? pred - diff : pred + diff;
        if (pred < -32768) pred = -32768;
        if (pred > 32767) pred = 32767;
        index += k_index[nib & 7];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        out[2 * m] = (int16_t)pred;
    }
    c->pred = pred;
    c->index = index;
}

static void intro_thread(void* arg) {
    uint32_t fade_left = FADE_FRAMES;
    (void)arg;
    while (s_run) {
        uint32_t space = xhw_audio_space(), n, k;
        if (space < CHUNK_FRAMES) {
            xhw_sleep_ms(30);
            continue;
        }
        n = CHUNK_PACKETS;
        if (s_pos + n * PACKET_BYTES > s_size) n = (s_size - s_pos) / PACKET_BYTES;
        if (n == 0 || !xhw_file_read(s_file, s_off + s_pos, s_packets, n * PACKET_BYTES)) {
            s_pos = 0;   /* the end, or a bad read: from the top, the headers starting over */
            s_ch[0].index = s_ch[1].index = -1;
            continue;
        }
        s_pos += n * PACKET_BYTES;
        for (k = 0; k < n; k++) {
            int16_t* out = s_buf + k * PACKET_FRAMES * 2;
            decode_channel(&s_ch[0], s_packets + k * PACKET_BYTES, out);
            decode_channel(&s_ch[1], s_packets + k * PACKET_BYTES + 34, out + 1);
        }
        n *= PACKET_FRAMES;
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

/* the footer make_intro.py writes at the end of default.xbe */
static int find_music(const char* path) {
    uint64_t size = 0, written;
    uint32_t foot[4];
    s_file = xhw_file_open(path);
    if (!s_file || !xhw_file_stamp(s_file, &size, &written) || size < 16 ||
        !xhw_file_read(s_file, (uint32_t)size - 16, foot, 16))
        return 0;
    if (memcmp(foot, "PXIN", 4) != 0 || foot[3] != 1 || (uint64_t)foot[1] + foot[2] + 16 != size ||
        foot[2] < PACKET_BYTES * 500)
        return 0;
    s_off = foot[1];
    s_size = foot[2] - foot[2] % PACKET_BYTES;
    return 1;
}

void xhw_intro_start(void) {
    char path[300];
    if (s_run) return;
    snprintf(path, sizeof path, "%sdefault.xbe", xhw_game_dir());
    if (!find_music(path)) {
        xhw_logf("[INTRO] no music at the end of %s: the check runs silent", path);
        return;
    }
    s_packets = (uint8_t*)malloc(CHUNK_PACKETS * PACKET_BYTES);
    s_buf = (int16_t*)malloc(CHUNK_FRAMES * 4);
    if (!s_packets || !s_buf || !xhw_audio_init(INTRO_RATE)) {
        xhw_logf("[INTRO] no audio: the check runs silent");
        free(s_packets);
        free(s_buf);
        s_packets = NULL;
        s_buf = NULL;
        return;
    }
    memset(s_ch, 0, sizeof s_ch);
    s_ch[0].index = s_ch[1].index = -1;   /* the first packet's header sets them */
    s_pos = 0;
    s_stop = 0;
    s_done = 0;
    s_run = 1;
    if (!xhw_thread_start(intro_thread, NULL, 0, 16 * 1024)) {
        s_run = 0;
        return;
    }
    xhw_logf("[INTRO] playing from %s (%u KB at %08x, %u s)", path, s_size >> 10, s_off,
             s_size / PACKET_BYTES * PACKET_FRAMES / INTRO_RATE);
}

void xhw_intro_stop(void) {
    int i;
    if (!s_run) return;
    s_stop = 1;
    for (i = 0; i < 300 && !s_done; i++) xhw_sleep_ms(10);   /* the second's fade, then out */
    s_run = 0;
    for (i = 0; i < 50 && !s_done; i++) xhw_sleep_ms(10);
    free(s_packets);
    free(s_buf);
    s_packets = NULL;
    s_buf = NULL;
    xhw_logf("[INTRO] stopped");
}
