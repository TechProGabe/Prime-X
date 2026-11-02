/* bench_menu.c - xhw_menu.c's card on the host (tools/xbox/bench_menu.py):
 * how long a full redraw and a selection move take at 480 and 720p, and the
 * card written out as a PPM to look at. The host is ~10x a 733 MHz P3; the
 * ratios between versions are what count. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xhw.h"

static xhw_video_mode g_vm;
const xhw_video_mode* xhw_video(void) { return &g_vm; }
uint32_t xhw_mem_free_kb(void) { return 30000; }
uint32_t xhw_mem_reclaim(uint32_t want, int urgent) { (void)want, (void)urgent; return 0; }
void xhw_logf(const char* fmt, ...) { (void)fmt; }
uint64_t xhw_time_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* the console is ~34x slower: its 3 ms budget is ~90 us here */
#define XHW_MENU_BUDGET_US 90
#define XHW_MENU_OPEN_BUDGET_US 300
#include "xhw_menu.c"

static void view(xhw_menu_view* v, int sel, int page) {
    int r;
    memset(v, 0, sizeof *v);
    v->tabs = 5;
    v->tab = page ? 2 : 0;
    v->page = page;
    strcpy(v->tab_name[0], "Video");
    strcpy(v->tab_name[1], "Controller");
    strcpy(v->tab_name[2], "Buttons");
    strcpy(v->tab_name[3], "Gameplay");
    strcpy(v->tab_name[4], "System");
    strcpy(v->status, "Now: 720p 16:9");
    v->rows = 5;
    v->sel = sel;
    for (r = 0; r < 5; r++) {
        sprintf(v->label[r], "Row number %d", r);
        strcpy(v->value[r], r & 1 ? "Medium" : "720p");
        v->choices[r] = 3;
        v->choice[r] = r % 3;
    }
    strcpy(v->help, "The picture signal the console sends. Hold BACK while Prime-X starts to get 480i back.");
    strcpy(v->value_help, "720p: HD, always 16:9. Needs component cables and 720p on in the dashboard.");
    v->pad_sel = sel;
    for (r = 0; r < XHW_MENU_IN_COUNT; r++) strcpy(v->pad_map[r], "Morph Ball");
}

/* frames until the card is whole again, and the slowest frame's ms */
static void run(const char* what, uint8_t* fb, int w, int h, int bpp, const xhw_menu_view* v) {
    uint64_t worst = 0, total = 0;
    int frames = 0;
    xhw_menu_show(v);
    do {
        uint64_t t0 = xhw_time_ns(), d;
        xhw_menu_draw(fb, w, h, bpp, w * bpp / 8);
        d = xhw_time_ns() - t0;
        total += d;
        if (d > worst) worst = d;
        frames++;
    } while (xhw_menu_busy() && frames < 1000);
    printf("%-28s %3d frames, %7.2f ms in all, slowest frame %6.2f ms\n", what, frames, total / 1e6, worst / 1e6);
}

int main(int argc, char** argv) {
    int m;
    for (m = 0; m < 2; m++) {
        int w = m ? 1280 : 640, h = m ? 720 : 480, bpp = m ? 16 : 32;
        uint8_t* fb = calloc((size_t)w * h, bpp / 8);
        xhw_menu_view v;
        g_vm.width = w, g_vm.height = h, g_vm.bpp = bpp, g_vm.widescreen = m;
        printf("%s\n", m ? "720p 16-bit" : "480 32-bit");
        view(&v, 0, 0);
        run("open (rows)", fb, w, h, bpp, &v);
        view(&v, 1, 0);
        run("move down", fb, w, h, bpp, &v);
        view(&v, 1, 1);
        run("Buttons tab", fb, w, h, bpp, &v);
        view(&v, 2, 1);
        run("Buttons: next input", fb, w, h, bpp, &v);
        if (argc > 1) {   /* the frame as a PPM */
            char name[300];
            FILE* f;
            int x, y;
            snprintf(name, sizeof name, "%s_%d.ppm", argv[1], h);
            f = fopen(name, "wb");
            fprintf(f, "P6 %d %d 255\n", w, h);
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    uint32_t c;
                    if (bpp == 16) {
                        uint16_t p = ((uint16_t*)fb)[y * w + x];
                        c = ((p >> 11) & 31) << 19 | ((p >> 5) & 63) << 10 | (p & 31) << 3;
                    } else {
                        c = ((uint32_t*)fb)[y * w + x];
                    }
                    fputc(c >> 16 & 255, f), fputc(c >> 8 & 255, f), fputc(c & 255, f);
                }
            fclose(f);
        }
        xhw_menu_show(NULL);
        free(fb);
    }
    return 0;
}
