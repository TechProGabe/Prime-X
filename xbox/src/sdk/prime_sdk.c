/* prime_sdk.c - the Dolphin SDK calls Prime makes that Melee never did, and
 * the aurora extensions MetroidPrimePort's game code calls (docs/reuse-map.md
 * "SDK surface"). Each one either does what the hardware did, as far as the
 * game can tell, or is a feature Prime-X leaves out (the GBA link, DTK disc
 * streaming, MetroidPrimePort's PBR and post-processing GX extensions). */
#include <dolphin/ai.h>
#include <dolphin/card.h>
#include <dolphin/dtk.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXShims.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <aurora/aurora.h>
#include <aurora/card.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xsdk.h"

/* ======================================================================
 * Frames: CGraphics::EndScene (DolphinCGraphics.cpp) copies the EFB out
 * (GXCopyDisp: the NV2A flip, gx_copy.c), then closes "the aurora frame":
 * that is the frame boundary here (vi.c: 60 Hz pacing, alarms, retrace
 * callbacks). It then arms a GP breakpoint at the FIFO's write pointer and
 * waits, at the next EndScene, for the breakpoint and a retrace to swap its
 * XFBs; the GP is never behind here, so the breakpoint fires at once and the
 * swap completes in the same call (GXEnableBreakPt).
 * ====================================================================== */
static const AuroraEvent s_no_events[1] = { { AURORA_NONE } };

const AuroraEvent* aurora_update(void) { return s_no_events; }
bool aurora_begin_frame(void) { return true; }
void aurora_end_frame(void) {
    static int first = 1;
    if (first) {
        /* main.dol (4 MB) was read for the default font, which the game
         * loaded before its first frame; it is read again if ever asked */
        first = 0;
        xsdk_dvd_free_dol();
    }
    xsdk_frame_boundary();
}
void aurora_get_pipeline_stats(AuroraPipelineStats* out) { memset(out, 0, sizeof *out); }
void aurora_pipeline_prefetch(uint64_t context) { (void)context; }
uint64_t aurora_pipeline_set_context(uint64_t context) { (void)context; return 0; }

static GXBreakPtCallback s_break_cb;
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb) {
    GXBreakPtCallback old = s_break_cb;
    s_break_cb = cb;
    return old;
}
void GXEnableBreakPt(void* breakPt) {
    (void)breakPt;
    if (s_break_cb) s_break_cb();
    xsdk_vi_flip_now();
}
void GXDisableBreakPt(void) {}

/* The FIFO is the NV2A pushbuffer: there is no GameCube FIFO to point at. */
static GXFifoObj s_fifo;
GXFifoObj* GXGetGPFifo(void) { return &s_fifo; }
void GXSetGPFifo(GXFifoObj* fifo) { (void)fifo; }
void GXSetCPUFifo(GXFifoObj* fifo) { (void)fifo; }
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) { (void)fifo; (void)base; (void)size; }
void GXInitFifoLimits(GXFifoObj* fifo, u32 hi, u32 lo) { (void)fifo; (void)hi; (void)lo; }
void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {
    (void)fifo;
    *readPtr = *writePtr = &s_fifo;
}

/* ---- GX state the back end doesn't need ---- */
void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout, u16 hor, u16 ver) {
    (void)hor; (void)ver;
    if (rmout != rmin) *rmout = *rmin;
}
void GXInitTexCacheRegion(GXTexRegion* region, GXBool is_32b_mipmap, u32 tmem_even, GXTexCacheSize size_even,
                          u32 tmem_odd, GXTexCacheSize size_odd) {
    (void)region; (void)is_32b_mipmap; (void)tmem_even; (void)size_even; (void)tmem_odd; (void)size_odd;
}
static GXTexRegionCallback s_region_cb;
GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback callback) {
    GXTexRegionCallback old = s_region_cb;
    s_region_cb = callback;
    return old;
}
void GXInitTexObjUserData(GXTexObj* obj, void* user_data) { (void)obj; (void)user_data; }
void GXDestroyTexObj(GXTexObj* obj) { (void)obj; }
void GXDestroyCopyTex(void* dest) { (void)dest; }
void GXSetArrayBaseIndex(GXAttr attr, u32 base) { (void)attr; (void)base; }
void GXSetTexCoordScaleManually(GXTexCoordID coord, GXBool enable, u16 ss, u16 ts) {
    (void)coord; (void)enable; (void)ss; (void)ts;
}
void GXSetDrawTag(u32 asset, u32 model, u32 material) { (void)asset; (void)model; (void)material; }
void GXSetHudSample(u8 mode, f32 x, f32 y) { (void)mode; (void)x; (void)y; }
void GXSetClipPlane(GXBool enable, const f32 plane[4]) { (void)enable; (void)plane; }
void GXCopyProbeFace(u32 face) { (void)face; }

/* The write-gather pipe: CGraphics streams immediate-mode vertex data
 * through it into a buffer. Writes go wherever the pointer points. */
static void* s_wgp;
volatile void* GXRedirectWriteGatherPipe(void* buf) {
    s_wgp = buf;
    return buf;
}
void GXRestoreWriteGatherPipe(void) { s_wgp = NULL; }

/* ---- MetroidPrimePort's renderer extensions: off ---- */
void GXSetPBR(GXBool enable) { (void)enable; }
void GXSetPBRAmbient(const f32 rows[6][3], f32 mode) { (void)rows; (void)mode; }
void GXSetPBRBacklight(const f32 plane[4], const f32 backDir[3], f32 back, f32 top, f32 bindScale, f32 bindOffset) {
    (void)plane; (void)backDir; (void)back; (void)top; (void)bindScale; (void)bindOffset;
}
void GXSetPBRCube(u32 id, const f32 params[4]) { (void)id; (void)params; }
void GXSetPBRLightHdr(GXLightID light, const f32 color[3], const f32 viewPos[3], f32 r0, f32 r1, u32 falloff) {
    (void)light; (void)color; (void)viewPos; (void)r0; (void)r1; (void)falloff;
}
void GXSetPBRLightScale(f32 diffuse, f32 f0, f32 alpha, GXBool alphaReplaces) {
    (void)diffuse; (void)f0; (void)alpha; (void)alphaReplaces;
}
void GXSetPBRLightSkip(u32 mask) { (void)mask; }
void GXSetPBRLightmap(u32 id, const f32 rect[4], const f32 axes[3][3]) { (void)id; (void)rect; (void)axes; }
void GXSetPBRLightmapAttr(GXAttr attr) { (void)attr; }
void GXSetPBRModelModulation(const f32 diffuse[4], const f32 incan[4], f32 coverage) {
    (void)diffuse; (void)incan; (void)coverage;
}
void GXSetPBRProbe(const f32 viewToProbe[3][3], f32 weight) { (void)viewToProbe; (void)weight; }
void GXSetPBRProbeEx(const f32 viewToProbe[3][3], f32 weight, f32 occlusionMin, f32 occlusionInvMax) {
    (void)viewToProbe; (void)weight; (void)occlusionMin; (void)occlusionInvMax;
}
void GXSetPBRShield(const f32 rows[8][4]) { (void)rows; }
void GXSetPBRThermal(GXBool on, f32 x, f32 y, GXBool blended) { (void)on; (void)x; (void)y; (void)blended; }
void GXSetPBRTone(const f32 rows[3][4]) { (void)rows; }
void GXSetPBRVolume(u32 id, const f32 rows[6][4]) { (void)id; (void)rows; }

void GXSetPBRMaterial(const f32 emissive[3], const f32 backlight[3], f32 heightBlend, f32 mode, const f32 layer[5],
                      const f32 kind[6], const f32 up[3]) {
    (void)emissive; (void)backlight; (void)heightBlend; (void)mode; (void)layer; (void)kind; (void)up;
}
/* scene HDR, post-processing, sun shadows, room lights, volumetric fog:
 * MetroidPrimePort's own renderer features, answered "not available" */
void GXPortCopyScene(void* dest, GXBool clear) { (void)dest; (void)clear; }
u32 GXPortCopySerial(void) { return 0; }
void GXPortDrawLog(GXBool on) { (void)on; }
u32 GXPortGetPerformance(void) { return 0; }
u32 GXPortGetRoomLights(f32 records[64 * 16]) { (void)records; return 0; }
u8 GXPortGetShadowCasterMode(void) { return 0; }
GXBool GXPortMeasureScene(f32 exposure) { (void)exposure; return GX_FALSE; }
GXBool GXPortPauseSceneHdr(void) { return GX_FALSE; }
void GXPortPostCompositeAdd(const f32 rgba[4]) { (void)rgba; }
void GXPortPostCompositeRadial(const f32 c0[4], const f32 c1[4], const f32 c2[4]) { (void)c0; (void)c1; (void)c2; }
GXBool GXPortPostProcessScene(GXBool bloom, f32 threshold, const f32 tints[5][3], const f32 tone[3][4], u32 gradeA,
                              u32 gradeB, f32 gradeWeight, f32 exposure) {
    (void)bloom; (void)threshold; (void)tints; (void)tone; (void)gradeA; (void)gradeB; (void)gradeWeight; (void)exposure;
    return GX_FALSE;
}
GXBool GXPortRenderShadowMap(void) { return GX_FALSE; }
GXBool GXPortSceneHdr(GXBool on) { (void)on; return GX_FALSE; }
void GXPortSetDepthPrepass(u8 pass) { (void)pass; }
void GXPortSetDrawSerial(u32 serial) { (void)serial; }
void GXPortSetParticleFog(GXBool on) { (void)on; }
void GXPortSetRoomLights(const f32* records, u32 count) { (void)records; (void)count; }
void GXPortSetShadowCaster(GXBool on) { (void)on; }
void GXPortSetShadowCasterOnly(GXBool on) { (void)on; }
void GXPortSetShadowFrame(const f32 worldToView[3][4], const f32 frustum[6], const f32 sunDir[3], f32 distance,
                          const f32 color[3]) {
    (void)worldToView; (void)frustum; (void)sunDir; (void)distance; (void)color;
}
GXBool GXPortShadowBoxCasts(const f32 min[3], const f32 max[3]) { (void)min; (void)max; return GX_FALSE; }
GXBool GXPortVolumetricFog(const GXPortFogParams* params) { (void)params; return GX_FALSE; }
void GXPortVolumetricFogEnd(void) {}

/* ARQ completions arrive with the alarms (ar.c); the main loop also asks */
void ARQPoll(void) { xsdk_arq_deliver(); }

/* ======================================================================
 * Locked cache: the Gekko's 16 KB of L1 data cache used as scratch memory,
 * filled by DMA (LCLoadData). Here a plain buffer and memcpy.
 * ====================================================================== */
static u8 s_lc[16 * 1024] __attribute__((aligned(32)));
void LCEnable(void) {}
void* LCGetBase(void) { return s_lc; }
u32 LCLoadData(void* dest, void* src, u32 nBytes) {
    memcpy(dest, src, nBytes);
    return (nBytes + 31) / 32;
}
u32 LCQueueLength(void) { return 0; }
void LCQueueWait(u32 len) { (void)len; }
void DCZeroRange(void* addr, u32 nBytes) { memset(addr, 0, nBytes); }
void PPCSetFpIEEEMode(void) {}

/* ======================================================================
 * OS
 * ====================================================================== */
static OSThread* s_main_thread = (OSThread*)&s_main_thread;
OSThread* OSGetCurrentThread(void) { return s_main_thread; }
/* The game spins on OSYieldThread while it waits for a DVD, ARAM or card
 * transfer; on the GameCube their interrupts landed meanwhile. Here
 * completions are delivered with the alarms (os.c), so a yield runs them. */
void OSYieldThread(void) {
    xsdk_os_yield();
    xsdk_run_alarms();
}
BOOL OSGetResetButtonState(void) { return FALSE; }
static void *s_save_start, *s_save_end;
void OSSetSaveRegion(void* start, void* end) {
    s_save_start = start;
    s_save_end = end;
}
void OSGetSavedRegion(void** start, void** end) {
    *start = NULL;   /* nothing survives a restart: there is no warm reset */
    *end = NULL;
}
static u32 s_sound_mode = OS_SOUND_MODE_STEREO;
u32 OSGetSoundMode(void) { return s_sound_mode; }
void OSSetSoundMode(u32 mode) { s_sound_mode = mode; }
BOOL OSLink(OSModuleInfo* newModule, void* bss) { (void)newModule; (void)bss; return FALSE; }
BOOL OSUnlink(OSModuleInfo* oldModule) { (void)oldModule; return FALSE; }
void OSProtectRange(u32 chan, void* addr, u32 nBytes, u32 control) { (void)chan; (void)addr; (void)nBytes; (void)control; }
u32 OSSaveContext(OSContext* context) { (void)context; return 0; }

/* ======================================================================
 * AI: the audio interface's DMA, as MetroidPrimePort drives it
 * (platform/ai_dma.cpp there). The streamed music (CStaticAudioPlayer: the
 * front end's, some rooms') and movie audio fill an AI buffer from a DMA
 * callback. There is no AI: the main loop runs the callback (AIPortPoll; the
 * game's mixers aren't thread-safe) and queues what it made here, 16-bit
 * stereo at 32 kHz like MusyX's output, which mixes it into each block it
 * hands the AC97 (px_ai_mix, from px_sdl3.cpp). The queue is kept ~200 ms
 * deep: the main loop runs at the frame rate, which can be 10 fps.
 *
 * On the GameCube MusyX's own AI callback submits a fresh mix buffer first
 * and the others read it back (AIGetDMAStartAddr) and mix on top; here
 * MusyX plays through its own path, so they are handed silence, zeroed
 * each time (a mixer that adds to its input would feed back otherwise).
 * ====================================================================== */
#define AI_RING 16384          /* stereo frames, a power of two */
#define AI_TARGET 6400         /* 200 ms at 32 kHz */
#define AI_MAX_DMA (0x7FFF * 32)
static AIDCallback s_ai_cb;
static uintptr_t s_ai_buf;
static u32 s_ai_len;
static int s_ai_playing = 1;
static __attribute__((aligned(32))) u8 s_ai_silence[0x280];
static int16_t s_ai_ring[AI_RING * 2];
static volatile uint32_t s_ai_w, s_ai_r;   /* frames written (main loop) / mixed (audio thread) */

static void ai_silence(void) {
    s_ai_buf = (uintptr_t)s_ai_silence;
    s_ai_len = sizeof s_ai_silence;
}

void AIInit(u8* stack) {
    (void)stack;
    ai_silence();
}
AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    AIDCallback old = s_ai_cb;
    s_ai_cb = callback;
    if (!old != !callback) xhw_logf("[AI] streamed audio %s", callback ? "starts" : "stops");
    ai_silence();   /* the previous owner may free its buffer now */
    s_ai_r = s_ai_w;   /* drop what the previous one queued */
    return old;
}
void AIInitDMA(uintptr_t start_addr, u32 length) {
    if (!start_addr || !length || length % 4 || length > AI_MAX_DMA) {
        ai_silence();
        return;
    }
    s_ai_buf = start_addr;
    s_ai_len = length;
}
u32 AIGetDMAStartAddr(void) { return (u32)s_ai_buf; }
uintptr_t AIPortGetDMAStartAddr(void) { return s_ai_buf; }
void AISetStreamPlayState(u32 state) {
    s_ai_playing = state != 0;
    if (!s_ai_playing) s_ai_r = s_ai_w;
}

void AIPortPoll(void) {
    int budget = 64;
    if (!s_ai_buf) ai_silence();
    while (s_ai_cb && s_ai_playing && budget-- > 0 && s_ai_w - s_ai_r < AI_TARGET) {
        const int16_t* src;
        uint32_t n, i, w;
        memset(s_ai_silence, 0, sizeof s_ai_silence);
        ai_silence();
        s_ai_cb();
        src = (const int16_t*)s_ai_buf;
        n = s_ai_len / 4;
        if (!src || !n || s_ai_w - s_ai_r + n > AI_RING) break;
        w = s_ai_w;
        for (i = 0; i < n; i++, w++) {
            s_ai_ring[(w & (AI_RING - 1)) * 2] = src[2 * i];
            s_ai_ring[(w & (AI_RING - 1)) * 2 + 1] = src[2 * i + 1];
        }
        s_ai_w = w;   /* after the samples: the audio thread reads up to here */
    }
}

/* MusyX's audio thread: the queued AI frames added into a block (saturating) */
void px_ai_mix(int16_t* buf, uint32_t frames) {
    uint32_t r = s_ai_r, have = s_ai_w - r, n = frames < have ? frames : have, i;
    for (i = 0; i < n; i++, r++) {
        const int16_t* s = &s_ai_ring[(r & (AI_RING - 1)) * 2];
        int32_t a = buf[2 * i] + s[0], b = buf[2 * i + 1] + s[1];
        buf[2 * i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        buf[2 * i + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
    s_ai_r = r;
}

/* ======================================================================
 * DTK (disc track streaming) and the GBA link: Prime streams its music as
 * DSP ADPCM files and only uses the GBA for the Fusion suit and NES Metroid
 * unlocks; neither device is there.
 * ====================================================================== */
void DTKInit(void) {}
u32 DTKQueueTrack(char* fileName, DTKTrack* track, u32 eventMask, DTKCallback callback) {
    (void)fileName; (void)track; (void)eventMask; (void)callback;
    return 1;
}
int DTKFlushTracks(DTKFlushCallback callback) {
    if (callback) callback();
    return 1;
}
int DTKSetState(u32 state) { (void)state; return 1; }
u32 DTKGetState(void) { return 0; }
int DTKNextTrack(void) { return 1; }
void DTKSetRepeatMode(u32 repeat) { (void)repeat; }
void DTKSetSampleRate(u32 rate) { (void)rate; }
void DTKSetVolume(u8 left, u8 right) { (void)left; (void)right; }

void GBAInit(void) {}
s32 GBAGetStatus(s32 chan, u8* status) { (void)chan; if (status) *status = 0; return 1; /* GBA_NOT_READY */ }
s32 GBAGetProcessStatus(s32 chan, u8* percentp) { (void)chan; if (percentp) *percentp = 0; return 1; }
s32 GBAJoyBootAsync(s32 chan, s32 palette_color, s32 palette_speed, u8* programp, s32 length, u8* status,
                    void* callback) {
    (void)chan; (void)palette_color; (void)palette_speed; (void)programp; (void)length; (void)status; (void)callback;
    return 1;
}
s32 GBARead(s32 chan, u8* dst, u8* status) { (void)chan; (void)dst; (void)status; return 1; }
s32 GBAWrite(s32 chan, u8* src, u8* status) { (void)chan; (void)src; (void)status; return 1; }
s32 GBAReset(s32 chan, u8* status) { (void)chan; (void)status; return 1; }

/* ======================================================================
 * Controllers: only the Xbox pads (pad.c); no keyboard bindings, and no
 * GameCube port overrides.
 * ====================================================================== */
u32 SIProbe(s32 chan) { return chan == 0 ? 0x09000000u /* SI_GC_CONTROLLER */ : 0; }
struct SDL_Gamepad* PADGetSDLGamepadForIndex(u32 index) { (void)index; return NULL; }
PADKeyButtonBinding* PADGetKeyButtonBindings(u32 port, u32* count) { (void)port; if (count) *count = 0; return NULL; }
PADKeyAxisBinding* PADGetKeyAxisBindingsSlot(u32 port, u32 slot, u32* count) {
    (void)port; (void)slot;
    if (count) *count = 0;
    return NULL;
}
void PADSetPortOverride(u32 port, const PADStatus* status) { (void)port; (void)status; }

/* ======================================================================
 * Memory card extras (card.c has the rest)
 * ====================================================================== */
void CARDSetBasePath(const char* path, s32 chan) { (void)path; (void)chan; }
s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
    (void)chan;
    *serialNo = 0x5058000150580001ull;   /* fixed: one card, the same every boot */
    return CARD_RESULT_READY;
}
AuroraCardType aurora_card_get_type(s32 channel) { (void)channel; return (AuroraCardType)0; }
size_t aurora_card_get_path(const char* gameName, AuroraCardType type, s32 channel, char* buffer, size_t capacity) {
    (void)gameName; (void)type; (void)channel;
    const char* p = xhw_save_dir();
    size_t n = strlen(p) + 1;
    if (n <= capacity) memcpy(buffer, p, n);
    return n;
}
bool aurora_card_set_base_path(const char* path) { (void)path; return true; }

/* THP: xbox/src/port/thp (aurora's decoder) */

/* ---- C library gaps (pdclib) ---- */
double frsqrte(double x) { return 1.0 / sqrt(x); }

/* ---- render modes the game names but never picks on an NTSC disc ---- */
extern GXRenderModeObj GXNtsc480IntDf;
GXRenderModeObj GXMpal480IntDf;
GXRenderModeObj GXPal528IntDf;
void xsdk_prime_sdk_init(void) {
    GXMpal480IntDf = GXNtsc480IntDf;
    GXPal528IntDf = GXNtsc480IntDf;
}
