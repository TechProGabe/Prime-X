/* xhw_code_repair.c - puts the game's code back the way default.xbe has it
 * (OpenCrossing-Xbox's xbox_code_repair.c, MIT, as Wings64-X carries it).
 *
 * On CPU-upgraded and overclocked consoles the BIOS patches the title's code
 * at load, by signature, to slow down games that time themselves with the
 * TSC. Two forms seen:
 * - OpenCrossing GitHub #2/#3 (a 1 GHz Coppermine, a 1.4 GHz Tualatin):
 *   `0f 31` (rdtsc) rewritten as `cd 2e` (int 2Eh), a trap each. It matched
 *   a pair in OpenCrossing that was no rdtsc at all, and the game faulted.
 * - A Cerbios console with a 1.4 GHz CPU (2026-11-01, the beta's "slow
 *   motion" report, docs/boot-safety.md): 5-byte `jmp` detours into the
 *   kernel (e9 rel32 over the first bytes of an instruction). One at a
 *   PCRTC interrupt-enable write even with AdvCPUSupport off; with it on,
 *   43 bytes, the first at an rdtsc, none of them `cd 2e`. Some rdtsc sites
 *   detoured and others not, two readings of the counter didn't agree:
 *   xhw_sys.c calibrated the TSC at 165 GHz, the clock jumped and froze,
 *   and the game ran a frame every 7 s. The old rule here ("at most 16
 *   bytes that aren't a rewritten rdtsc, else not a patcher") left them.
 * Prime-X keeps time with the ACPI timer (xhw_sys.c), but its .text has ~80
 * `0f 31` pairs: the clock's, the profiler's and the interrupt lock's rdtsc
 * (xhw_sys.c, xhw_perf.c, os.c), run thousands of times a frame (each a
 * trap once rewritten), and byte pairs inside other instructions.
 *
 * Nothing in .text is ours to have changed after load (no relocations; the
 * kernel's import thunks are in .rdata), so at boot every byte that differs
 * from the file is put back, before anything reads the TSC, whatever the
 * patcher wrote. D:\default.xbe must be the running build (its header's
 * time stamps and size, else an A/B build in another folder would get this
 * one's bytes), and at most MAX_FIX bytes may differ (more isn't a patcher:
 * logged, left alone). boot.log gets every run of changed bytes, the
 * memory's and the file's, so a new patcher's form shows at a glance.
 * Switches: -DXHW_CODE_REPAIR=0 (not built), =2 (check and log only),
 * -DXHW_CODE_REPAIR_TEST (patch two sites the ways the BIOSes do, then
 * repair). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_CODE_REPAIR
#define XHW_CODE_REPAIR 1
#endif

#if XHW_CODE_REPAIR
#define XBE_PATH "D:\\default.xbe"
#define CHUNK 65536
#define MAX_FIX 1024  /* differing bytes kept; more and it isn't a patcher (every rdtsc detoured would be 400) */
#define LOG_RUNS 24   /* runs of changed bytes logged */
#define LOG_BYTES 8   /* bytes of a run shown */

static struct {
    unsigned va;
    unsigned char ram, disk;
} s_fix[MAX_FIX];

/* one byte of code. The kernel maps .text read-only and CR0.WP makes that
 * hold in ring 0 too (MmSetAddressProtect on it faulted the write in xemu),
 * so WP is cleared around the store, with interrupts off. */
static void put_byte(unsigned va, unsigned char v) {
    unsigned cr0, tmp;
    __asm__ volatile("pushfl\n\t"
                     "cli\n\t"
                     "movl %%cr0, %0\n\t"
                     "movl %0, %1\n\t"
                     "andl $0xfffeffff, %1\n\t"
                     "movl %1, %%cr0\n\t"
                     "movb %b3, (%2)\n\t"
                     "movl %0, %%cr0\n\t"
                     "popfl"
                     : "=&r"(cr0), "=&r"(tmp)
                     : "r"(va), "q"(v)
                     : "memory", "cc");
}

/* the .text section header, from the image header the kernel loaded */
static const XBE_SECTION_HEADER* text_section(void) {
    const XBE_FILE_HEADER* h = (const XBE_FILE_HEADER*)0x00010000;
    const XBE_SECTION_HEADER* s = h->PointerToSectionTable;
    unsigned i;
    for (i = 0; i < h->NumberOfSections; i++, s++)
        if (s->SectionName && strcmp((const char*)s->SectionName, ".text") == 0) return s;
    return NULL;
}

#ifdef XHW_CODE_REPAIR_TEST
/* test builds: rewrite the first rdtsc the way the trap BIOSes do (cd 2e)
 * and the second the way Cerbios does (a 5-byte jmp over it), so a boot in
 * xemu or on a stock console shows the repair. Both are put back before
 * they could run. */
static void fake_patch(const XBE_SECTION_HEADER* s) {
    const unsigned char* p = (const unsigned char*)s->VirtualAddress;
    static const unsigned char jmp[5] = { 0xe9, 0x18, 0x7b, 0xe2, 0x7f };
    unsigned k, found = 0, i;
    for (k = 0; k + 5 <= s->FileSize && found < 2; k++) {
        if (p[k] != 0x0f || p[k + 1] != 0x31) continue;
        if (found == 0) {
            put_byte(s->VirtualAddress + k, 0xcd);
            put_byte(s->VirtualAddress + k + 1, 0x2e);
            xhw_logf("[BOOT] code check test: patched %08x like a trapping BIOS (cd 2e)", s->VirtualAddress + k);
        } else {
            for (i = 0; i < 5; i++) put_byte(s->VirtualAddress + k + i, jmp[i]);
            xhw_logf("[BOOT] code check test: patched %08x like Cerbios (a 5-byte jmp)", s->VirtualAddress + k);
        }
        found++;
    }
    if (!found) xhw_logf("[BOOT] code check test: no 0f 31 found");
}
#endif

/* the run of changed bytes from s_fix[i] on begins at an rdtsc in the file:
 * the file's bytes are 0f 31 there (the second from RAM when the run is one
 * byte: a byte outside the run is the file's), or it is the 31 of one */
static int run_at_rdtsc(unsigned i, unsigned len) {
    unsigned va = s_fix[i].va;
    unsigned char b0 = s_fix[i].disk, b1 = len > 1 ? s_fix[i + 1].disk : *(const unsigned char*)(va + 1);
    if (b0 == 0x0f && b1 == 0x31) return 1;
    return b0 == 0x31 && *(const unsigned char*)(va - 1) == 0x0f;
}

/* "e9 18 7b e2 7f" for up to LOG_BYTES bytes of a run, "+n" for the rest */
static void run_hex(char* out, size_t cap, unsigned i, unsigned len, int ram) {
    unsigned k, n = 0;
    out[0] = 0;
    for (k = 0; k < len && k < LOG_BYTES && n + 4 < cap; k++) {
        if (k) out[n++] = ' ';
        n += (unsigned)snprintf(out + n, cap - n, "%02x", ram ? s_fix[i + k].ram : s_fix[i + k].disk);
    }
    if (len > LOG_BYTES && n + 8 < cap) snprintf(out + n, cap - n, " +%u", len - LOG_BYTES);
}

void xhw_code_repair(void) {
    const XBE_SECTION_HEADER* s = text_section();
    const XBE_FILE_HEADER* img = (const XBE_FILE_HEADER*)0x00010000;
    XBE_FILE_HEADER fh;
    DWORD t0 = GetTickCount(), got;
    HANDLE h;
    unsigned char* buf;
    unsigned off, n, diff = 0, i, len, runs = 0, rdtsc_runs = 0, logged;
    int ok = 1;

    if (!s) {
        xhw_logf("[BOOT] code check: no .text section in the image header");
        return;
    }
#ifdef XHW_CODE_REPAIR_TEST
    fake_patch(s);
#endif
    h = CreateFileA(XBE_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        xhw_logf("[BOOT] code check: could not open %s (error %lu)", XBE_PATH, (unsigned long)GetLastError());
        return;
    }
    if (!ReadFile(h, &fh, sizeof fh, &got, NULL) || got != sizeof fh || fh.TimeDateStamp != img->TimeDateStamp ||
        fh.PeTimeDateStamp != img->PeTimeDateStamp || fh.SizeOfImage != img->SizeOfImage) {
        xhw_logf("[BOOT] code check: %s is not the running build (time stamp %08lx, running %08lx), left alone",
                 XBE_PATH, (unsigned long)fh.TimeDateStamp, (unsigned long)img->TimeDateStamp);
        CloseHandle(h);
        return;
    }
    buf = malloc(CHUNK);
    if (!buf || SetFilePointer(h, (LONG)s->FileAddress, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        free(buf);
        CloseHandle(h);
        return;
    }
    n = s->FileSize;
    for (off = 0; off < n; off += CHUNK) {
        unsigned len = n - off < CHUNK ? n - off : CHUNK, k;
        const unsigned char* ram = (const unsigned char*)(s->VirtualAddress + off);
        if (!ReadFile(h, buf, len, &got, NULL) || got != len) {
            ok = 0;
            break;
        }
        if (memcmp(ram, buf, len) == 0) continue;
        for (k = 0; k < len; k++) {
            if (ram[k] == buf[k]) continue;
            if (diff < MAX_FIX) {
                s_fix[diff].va = s->VirtualAddress + off + k;
                s_fix[diff].ram = ram[k];
                s_fix[diff].disk = buf[k];
            }
            diff++;
        }
    }
    free(buf);
    CloseHandle(h);

    if (!ok) {
        xhw_logf("[BOOT] code check: read of %s failed at .text+%x (error %lu)", XBE_PATH, off,
                 (unsigned long)GetLastError());
        return;
    }
    if (!diff) {
        xhw_logf("[BOOT] code check: .text matches %s (%lu ms)", XBE_PATH, (unsigned long)(GetTickCount() - t0));
        return;
    }
    if (diff > MAX_FIX) {
        xhw_logf("[BOOT] code check: %u bytes of .text differ from %s, first %08x; too many for a patcher, left alone",
                 diff, XBE_PATH, s_fix[0].va);
        return;
    }
    /* runs of consecutive changed bytes: a patcher's units (a 2-byte
     * rewrite, a 5-byte jmp), counted before the repair (run_at_rdtsc reads
     * RAM outside the runs, which is the file's either way) */
    for (i = 0; i < diff; i += len) {
        for (len = 1; i + len < diff && s_fix[i + len].va == s_fix[i].va + len; len++) {}
        runs++;
        if (run_at_rdtsc(i, len)) rdtsc_runs++;
    }
    if (XHW_CODE_REPAIR != 2)
        for (i = 0; i < diff; i++) put_byte(s_fix[i].va, s_fix[i].disk);
    xhw_logf("[BOOT] code check: %u bytes of .text were changed in memory after load (%u places, %u at an rdtsc; a BIOS "
             "patcher), %s (%lu ms)",
             diff, runs, rdtsc_runs, XHW_CODE_REPAIR != 2 ? "put back" : "left alone (XHW_CODE_REPAIR=2)",
             (unsigned long)(GetTickCount() - t0));
    for (i = 0, logged = 0; i < diff && logged < LOG_RUNS; i += len, logged++) {
        char ram[64], disk[64];
        int stuck = 1;
        unsigned k;
        for (len = 1; i + len < diff && s_fix[i + len].va == s_fix[i].va + len; len++) {}
        for (k = 0; k < len; k++)
            if (*(volatile unsigned char*)s_fix[i + k].va != s_fix[i + k].disk) stuck = 0;
        run_hex(ram, sizeof ram, i, len, 1);
        run_hex(disk, sizeof disk, i, len, 0);
        xhw_logf("[BOOT]   %08x: %s, file %s%s", s_fix[i].va, ram, disk,
                 XHW_CODE_REPAIR != 2 && !stuck ? " (write did not stick)" : "");
    }
    if (runs > LOG_RUNS) xhw_logf("[BOOT]   ... %u more places", runs - LOG_RUNS);
}
#else
void xhw_code_repair(void) {}
#endif
