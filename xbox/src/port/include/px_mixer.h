/* px_mixer.h - the MusyX software mixer's inner loops in 32-bit arithmetic
 * (extern/musyx/src/musyx/runtime/hw_pc.c, TARGET_XBOX; -DPX_MIXER_FAST=0
 * keeps hw_pc.c's own loops).
 *
 * hw_pc.c mixes every voice into up to nine buffers a 32-sample subframe
 * (main, aux A and aux B; left, right and surround), each sample through
 * two 64-bit products and two 64-bit clamps, and resamples it through four
 * more 64-bit products: on the Pentium III that is most of the mixer
 * thread's time, which on the GameCube the DSP spent. The versions here
 * are bit-exact with hw_pc.c's: a loop takes its 32-bit path only when the
 * operands' bounds (checked once a call) prove that no product or sum can
 * leave 32 bits, and hw_pc.c's arithmetic otherwise. The envelope, shared
 * by the nine buffers, is computed once a subframe.
 *
 * tools/xbox/test_mixer.py compiles this header against copies of hw_pc.c's
 * loops and compares them on random voices. */
#ifndef PX_MIXER_H
#define PX_MIXER_H

#include <stdint.h>

#include "musyx/pc_audio_math.h"

#define PX_MIX_MAX 160 /* samples a call at most (a frame) */

/* the ADSR envelope a sample, as mixRampChannel computes it */
static inline void pxMixEnvelope(int32_t* env, int n, int16_t envStart, int16_t envDelta) {
  int32_t e = envStart;
  for (int i = 0; i < n; ++i, e += envDelta)
    env[i] = e < 0 ? 0 : e > 0x7FFF ? 0x7FFF : e;
}

/* the largest |sample| of a voice's resampled block */
static inline uint32_t pxMixMaxAbs(const int32_t* src, int n) {
  uint32_t m = 0;
  for (int i = 0; i < n; ++i) {
    const uint32_t a = src[i] < 0 ? 0u - (uint32_t)src[i] : (uint32_t)src[i];
    if (a > m)
      m = a;
  }
  return m;
}

/* hw_pc.c's mixRampChannel with the envelope precomputed (pxMixEnvelope)
 * and the source's largest |sample| (pxMixMaxAbs) */
static inline void pxMixRamp(int32_t* dest, const int32_t* src, int n, int16_t startVol, int16_t deltaVol,
                             const int32_t* env, uint32_t srcMax) {
  if (dest == 0 || (startVol == 0 && deltaVol == 0) || n <= 0)
    return;
  const int32_t v0 = startVol, v1 = (int32_t)startVol + (int32_t)deltaVol * (n - 1);
  const uint32_t a0 = v0 < 0 ? (uint32_t)-v0 : (uint32_t)v0, a1 = v1 < 0 ? (uint32_t)-v1 : (uint32_t)v1;
  const uint32_t maxV = a0 > a1 ? a0 : a1;
  /* vol * env < 2^31 (env <= 0x7FFF), so the gain is exact in 32 bits and
   * |gain| <= maxV; then |src * gain| < 2^31 when srcMax * maxV is */
  if (maxV <= 65535u && (uint64_t)srcMax * maxV < 0x80000000ull) {
    int32_t vol = startVol;
    for (int i = 0; i < n; ++i, vol += deltaVol) {
      const int32_t gain = (vol * env[i]) >> 15;
      const int32_t term = (src[i] * gain) >> 15;
      int32_t sum;
      if (__builtin_add_overflow(dest[i], term, &sum))
        sum = term < 0 ? INT32_MIN : INT32_MAX;
      dest[i] = sum;
    }
    return;
  }
  for (int i = 0; i < n; ++i) {
    const int32_t vol = (int32_t)startVol + (int32_t)deltaVol * i;
    const int32_t gain = musyxPcScaleQ15(vol, env[i]);
    dest[i] = musyxPcClamp32((int64_t)dest[i] + musyxPcScaleQ15(src[i], gain));
  }
}

/* The polyphase resampler's four-tap sum: 32 bits are enough when every
 * phase's taps sum to under 2^31 / 32768 in absolute value (checked once,
 * pxMixTapsFit32, when the table is built). */
static inline int pxMixTapsFit32(const int16_t (*table)[4], int phases) {
  for (int p = 0; p < phases; ++p) {
    int32_t s = 0;
    for (int k = 0; k < 4; ++k)
      s += table[p][k] < 0 ? -table[p][k] : table[p][k];
    if ((int64_t)s * 32768 >= 0x80000000ll)
      return 0;
  }
  return 1;
}

/* DSP-ADPCM, musyxPcAdpcmSample in 32 bits: nibble x scale x 2048 is at
 * most 2^29 and |c0 h1 + c1 h2| at most 32768 (|c0| + |c1|), so the sum
 * stays inside 32 bits when every predictor's |c0| + |c1| is under 49152
 * (real coefficient tables are a few thousand: pxAdpcmFits32, once a fill) */
static inline int pxAdpcmFits32(const int16_t coefficients[8][2]) {
  for (int p = 0; p < 8; ++p) {
    const int32_t a = coefficients[p][0] < 0 ? -coefficients[p][0] : coefficients[p][0];
    const int32_t b = coefficients[p][1] < 0 ? -coefficients[p][1] : coefficients[p][1];
    if (a + b >= 49152)
      return 0;
  }
  return 1;
}

static inline int16_t pxAdpcmSample32(const uint8_t* block, unsigned sample, uint8_t ps,
                                      const int16_t coefficients[8][2], int16_t* history1, int16_t* history2) {
  const unsigned predictor = (ps >> 4) & 7;
  const int32_t scale = 1 << (ps & 15);
  int32_t nibble = (sample & 1) ? block[1 + sample / 2] & 15 : block[1 + sample / 2] >> 4;
  if (nibble >= 8) nibble -= 16;
  const int32_t value = (nibble * scale * 2048 + (int32_t)coefficients[predictor][0] * *history1 +
                         (int32_t)coefficients[predictor][1] * *history2 + 1024) >> 11;
  const int16_t decoded = value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
  *history2 = *history1;
  *history1 = decoded;
  return decoded;
}

#endif
