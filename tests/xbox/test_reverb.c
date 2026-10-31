/* test_reverb.c - creverb.c's PortHandleReverbChannel (TARGET_XBOX) against
 * its HandleReverbChannel: the same output samples and delay-line state, bit
 * for bit (tools/xbox/test_mixer.py puts creverb.c's C part above this). */
static uint32_t s_rng = 777;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static void make_line(_SND_REVSTD_DELAYLINE* a, _SND_REVSTD_DELAYLINE* b, s32 n) {
  s32 i;
  a->inputs = (f32*)malloc(n * sizeof(f32));
  b->inputs = (f32*)malloc(n * sizeof(f32));
  for (i = 0; i < n; ++i) a->inputs[i] = b->inputs[i] = (f32)((s32)(rnd() % 20001) - 10000);
  a->length = b->length = n * 4;
  a->inPoint = b->inPoint = (s32)(rnd() % n) * 4;
  a->outPoint = b->outPoint = (s32)(rnd() % n) * 4;
  a->lastOutput = b->lastOutput = (f32)((s32)(rnd() % 2001) - 1000);
}

int main(void) {
  long fails = 0, trials = 0;
  int t, k;
  for (t = 0; t < 300; ++t) {
    static _SND_REVSTD_WORK A, B;
    static s32 sa[480], sb[480];
    static f32 preA[3][64], preB[3][64];
    int frame, i;
    memset(&A, 0, sizeof A);
    for (i = 0; i < 6; ++i) {
      const s32 n = 37 + (s32)(rnd() % 900);
      make_line(&A.AP[i], &B.AP[i], n);
      make_line(&A.C[i], &B.C[i], n + 13);
    }
    A.allPassCoeff = 0.7f;
    for (i = 0; i < 6; ++i) A.combCoef[i] = 0.5f + (rnd() % 400) / 1000.0f;
    for (i = 0; i < 3; ++i) A.lpLastout[i] = (f32)(rnd() % 100);
    A.level = (rnd() % 1000) / 1000.0f;
    A.damping = (rnd() % 1000) / 1000.0f;
    A.preDelayTime = (t & 1) ? 1 + (s32)(rnd() % 63) : 0;
    for (k = 0; k < 3; ++k) {
      for (i = 0; i < 64; ++i) preA[k][i] = preB[k][i] = (f32)(rnd() % 3000);
      A.preDelayLine[k] = preA[k];
      A.preDelayPtr[k] = preA[k] + (A.preDelayTime ? rnd() % A.preDelayTime : 0);
    }
    {   /* B: A's copy with its own buffers */
      _SND_REVSTD_DELAYLINE ap[6], c[6];
      memcpy(ap, B.AP, sizeof ap);
      memcpy(c, B.C, sizeof c);
      B = A;
      memcpy(B.AP, ap, sizeof ap);
      memcpy(B.C, c, sizeof c);
      for (k = 0; k < 3; ++k) {
        B.preDelayLine[k] = preB[k];
        B.preDelayPtr[k] = preB[k] + (A.preDelayPtr[k] - preA[k]);
      }
    }
    for (frame = 0; frame < 20; ++frame) {
      for (i = 0; i < 480; ++i) sa[i] = sb[i] = (s32)(rnd() % 200001) - 100000;
      for (k = 0; k < 3; ++k) {
        HandleReverbChannel(sa + 160 * k, &A, k);
        PortHandleReverbChannel(sb + 160 * k, &B, k);
      }
      ++trials;
      if (memcmp(sa, sb, sizeof sa) != 0 || memcmp(A.lpLastout, B.lpLastout, sizeof A.lpLastout) != 0) {
        if (fails++ < 5) printf("reverb mismatch: trial %d frame %d\n", t, frame);
      }
      for (i = 0; i < 6; ++i)
        if (A.AP[i].inPoint != B.AP[i].inPoint || A.AP[i].outPoint != B.AP[i].outPoint ||
            A.C[i].lastOutput != B.C[i].lastOutput || A.C[i].inPoint != B.C[i].inPoint) {
          if (fails++ < 5) printf("reverb state mismatch: trial %d frame %d line %d\n", t, frame, i);
        }
    }
  }
  printf("reverb: %ld frames, %ld mismatches\n", trials, fails);
  return fails ? 1 : 0;
}
