/* test_mixer.c - px_mixer.h against hw_pc.c's own mixer loops (cut out of
 * extern/musyx/src/musyx/runtime/hw_pc.c by tools/xbox/test_mixer.py, which
 * puts them and the polyphase table builder above this file). */

static uint32_t s_rng = 12345;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}
/* mostly ordinary values, sometimes the edges */
static int32_t pick16(void) {
  switch (rnd() % 8) {
  case 0: return 32767;
  case 1: return -32768;
  case 2: return 0;
  default: return (int16_t)rnd();
  }
}

int main(void) {
  static int32_t src[160], destA[160], destB[160], env[160];
  long fast = 0, slow = 0, fails = 0, trials = 0;
  int i, t, n;

  /* the ramp: random voices, volumes, envelopes and buffers that already hold a mix */
  for (t = 0; t < 400000; ++t) {
    const int16_t startVol = (int16_t)pick16(), deltaVol = (int16_t)((rnd() % 4) ? (int32_t)(rnd() % 819) - 409 : pick16());
    const int16_t envStart = (int16_t)pick16(), envDelta = (int16_t)((rnd() % 4) ? (int32_t)(rnd() % 2049) - 1024 : pick16());
    const int big = rnd() % 16 == 0; /* a filtered voice: past s16 */
    n = (rnd() % 3) ? 32 : 1 + (int)(rnd() % 160);
    for (i = 0; i < n; ++i) {
      src[i] = big ? (int32_t)(rnd() % 400000) - 200000 : pick16();
      destA[i] = destB[i] = (rnd() % 64) ? (int32_t)(rnd() % 2000000) - 1000000 : (int32_t)rnd();
    }
    mixRampChannel(destA, src, n, startVol, deltaVol, envStart, envDelta);
    pxMixEnvelope(env, n, envStart, envDelta);
    {
      const uint32_t m = pxMixMaxAbs(src, n);
      const int32_t v1 = (int32_t)startVol + (int32_t)deltaVol * (n - 1);
      const uint32_t a0 = startVol < 0 ? -(int32_t)startVol : startVol, a1 = v1 < 0 ? -v1 : v1;
      const uint32_t mv = a0 > a1 ? a0 : a1;
      if (mv <= 65535u && (uint64_t)m * mv < 0x80000000ull) ++fast;
      else ++slow;
      pxMixRamp(destB, src, n, startVol, deltaVol, env, m);
    }
    ++trials;
    if (memcmp(destA, destB, (size_t)n * sizeof destA[0]) != 0) {
      if (fails++ < 5) {
        for (i = 0; i < n && destA[i] == destB[i]; ++i) {}
        printf("ramp mismatch: n %d vol %d+%d env %d+%d sample %d: src %d hw_pc %d px_mixer %d\n", n, startVol,
               deltaVol, envStart, envDelta, i, src[i], destA[i], destB[i]);
      }
    }
  }
  printf("ramp: %ld trials (%ld on the 32-bit path, %ld on the 64-bit one), %ld mismatches\n", trials, fast, slow,
         fails);

  /* the polyphase taps */
  initResampleTables();
  {
    const int fits = pxMixTapsFit32((const int16_t(*)[4])polyphaseTable, POLYPHASE_PHASES);
    long tapFails = 0, tapTrials = 0;
    printf("polyphase table: taps fit 32 bits: %s\n", fits ? "yes" : "NO (the resampler keeps 64 bits)");
    if (fits) {
      for (t = 0; t < 2000000; ++t) {
        const int p = (int)(rnd() % POLYPHASE_PHASES);
        const int16_t* c = polyphaseTable[p];
        const int32_t u0 = pick16(), u1 = pick16(), u2 = pick16(), u3 = pick16();
        const long long ref = ((long long)u0 * c[0] + (long long)u1 * c[1] + (long long)u2 * c[2] +
                               (long long)u3 * c[3]) >> 15;
        const int32_t got = (u0 * c[0] + u1 * c[1] + u2 * c[2] + u3 * c[3]) >> 15;
        ++tapTrials;
        if (clamp16((s32)ref) != clamp16(got) && tapFails++ < 5)
          printf("taps mismatch: phase %d\n", p);
      }
      /* the worst case: every sample at full scale with the taps' signs */
      for (int p = 0; p < POLYPHASE_PHASES; ++p) {
        const int16_t* c = polyphaseTable[p];
        for (int sgn = 0; sgn < 2; ++sgn) {
          int32_t u[4];
          for (int k = 0; k < 4; ++k) u[k] = (c[k] < 0) ^ sgn ? -32768 : 32767;
          const long long ref = ((long long)u[0] * c[0] + (long long)u[1] * c[1] + (long long)u[2] * c[2] +
                                 (long long)u[3] * c[3]) >> 15;
          const int32_t got = (u[0] * c[0] + u[1] * c[1] + u[2] * c[2] + u[3] * c[3]) >> 15;
          ++tapTrials;
          if (clamp16((s32)ref) != clamp16(got) && tapFails++ < 5) printf("taps mismatch at full scale: phase %d\n", p);
        }
      }
      printf("taps: %ld trials, %ld mismatches\n", tapTrials, tapFails);
      fails += tapFails;
    }
  }
  /* the resampler loop: pxMixResample4 against resampleVoice's own loop
   * (mode 0, its 32-bit taps), copied here verbatim but for the state */
  if (pxMixTapsFit32((const int16_t(*)[4])polyphaseTable, POLYPHASE_PHASES)) {
    static int32_t rsrc[1024], outA[160], outB[160];
    long rfails = 0, rtrials = 0;
    for (t = 0; t < 100000; ++t) {
      const uint32_t count = rnd() % 600, pitch = (rnd() % 8) ? 0x4000 + rnd() % 0x18000 : rnd() % 0x40000;
      uint32_t posA = rnd() & 0xFFFF, posB = posA, kA = rnd() % 40, kB = kA;
      int16_t histA[4], histB[4];
      n = 1 + (int)(rnd() % 160);
      for (i = 0; i < (int)count; ++i) rsrc[i] = (rnd() % 16) ? pick16() : (int32_t)(rnd() % 200000) - 100000;
      for (i = 0; i < 4; ++i) histA[i] = histB[i] = (int16_t)pick16();
      { /* resampleVoice, mode 0, polyphaseFits32 */
        s16 temp[4];
        u32 idx = 0;
        temp[idx++ & 3] = histA[0];
        temp[idx++ & 3] = histA[1];
        temp[idx++ & 3] = histA[2];
        temp[idx++ & 3] = histA[3];
        for (i = 0; i < n; ++i) {
          posA += pitch;
          while (posA >= 0x10000) {
            s32 sample = 0;
            if (kA < count) sample = rsrc[kA++];
            temp[idx++ & 3] = clamp16(sample);
            posA -= 0x10000;
          }
          const u32 phase = (posA & 0xFFFF) >> 9;
          const s16* c = polyphaseTable[phase];
          const s32 u0 = temp[idx++ & 3];
          const s32 u1 = temp[idx++ & 3];
          const s32 u2 = temp[idx++ & 3];
          const s32 u3 = temp[idx++ & 3];
          outA[i] = clamp16((u0 * c[0] + u1 * c[1] + u2 * c[2] + u3 * c[3]) >> 15);
        }
        histA[3] = temp[--idx & 3];
        histA[2] = temp[--idx & 3];
        histA[1] = temp[--idx & 3];
        histA[0] = temp[--idx & 3];
      }
      pxMixResample4(outB, n, pitch, &posB, rsrc, &kB, count, histB, (const int16_t(*)[4])polyphaseTable);
      ++rtrials;
      if (memcmp(outA, outB, (size_t)n * sizeof outA[0]) != 0 || posA != posB || kA != kB ||
          memcmp(histA, histB, sizeof histA) != 0) {
        if (rfails++ < 5) printf("resample mismatch: n %d pitch %x\n", n, pitch);
      }
    }
    printf("resample: %ld trials, %ld mismatches\n", rtrials, rfails);
    fails += rfails;
  }
  /* DSP-ADPCM: pxAdpcmSample32 against musyxPcAdpcmSample wherever
   * pxAdpcmFits32 allows it, edges included */
  {
    long afails = 0, atrials = 0;
    for (t = 0; t < 200000; ++t) {
      int16_t coef[8][2], h1a, h2a, h1b, h2b;
      uint8_t block[8];
      const int edge = rnd() % 8 == 0;
      int p;
      for (p = 0; p < 8; ++p) {
        coef[p][0] = (int16_t)(edge ? ((rnd() & 1) ? 32767 : -32768) : (int32_t)(rnd() % 16385) - 8192);
        coef[p][1] = (int16_t)(edge ? ((rnd() & 1) ? 16383 : -16384) : (int32_t)(rnd() % 16385) - 8192);
      }
      if (!pxAdpcmFits32((const int16_t(*)[2])coef)) continue;
      for (i = 0; i < 8; ++i) block[i] = (uint8_t)rnd();
      h1a = h1b = (int16_t)pick16();
      h2a = h2b = (int16_t)pick16();
      for (i = 0; i < 14; ++i) {
        const uint8_t ps = (uint8_t)((rnd() & 0x70) | (edge ? 15 : rnd() % 16));
        const int16_t a = musyxPcAdpcmSample(block, (unsigned)i, ps, (const int16_t(*)[2])coef, &h1a, &h2a);
        const int16_t b = pxAdpcmSample32(block, (unsigned)i, ps, (const int16_t(*)[2])coef, &h1b, &h2b);
        ++atrials;
        if (a != b || h1a != h1b || h2a != h2b) {
          if (afails++ < 5) printf("adpcm mismatch: %d vs %d\n", a, b);
          h1b = h1a;
          h2b = h2a;
        }
      }
    }
    printf("adpcm: %ld samples, %ld mismatches\n", atrials, afails);
    fails += afails;
  }
  return fails ? 1 : 0;
}
