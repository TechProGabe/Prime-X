// px_sdl3.cpp - the SDL3 subset in xbox/include/sdk/SDL3/px_sdl3.h, on the
// Xbox layer: time, threads and locks from xhw, MusyX's output stream into
// the AC97 ring (xhw_audio.c), message boxes to the log.
#include <SDL3/SDL.h>
#include <stdlib.h>

#include "xhw.h"

extern "C" {

const char* px_SDL_GetError(void) { return "not available on the Xbox"; }
bool px_SDL_InitSubSystem(Uint32) { return true; }
void px_SDL_QuitSubSystem(Uint32) {}

// ---- time ----
Uint64 px_SDL_GetTicksNS(void) { return xhw_time_ns(); }
Uint64 px_SDL_GetTicks(void) { return xhw_time_ns() / 1000000u; }
void px_SDL_Delay(Uint32 ms) { xhw_sleep_ms(ms); }
void px_SDL_DelayNS(Uint64 ns) { xhw_sleep_ms((Uint32)((ns + 999999u) / 1000000u)); }
void px_SDL_DelayPrecise(Uint64 ns) {
  const Uint64 end = xhw_time_ns() + ns;
  if (ns > 2000000u)
    xhw_sleep_ms((Uint32)(ns / 1000000u) - 1);
  while (xhw_time_ns() < end)
    xhw_yield();
}

// ---- threads and locks ----
struct SDL_Thread {
  SDL_ThreadFunction fn;
  void* data;
  int status;
  xhw_event* done;
};

static void thread_entry(void* arg) {
  SDL_Thread* t = static_cast< SDL_Thread* >(arg);
  t->status = t->fn(t->data);
  xhw_event_signal(t->done);
}

SDL_Thread* px_SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data) {
  SDL_Thread* t = static_cast< SDL_Thread* >(calloc(1, sizeof(SDL_Thread)));
  if (t == nullptr)
    return nullptr;
  t->fn = fn;
  t->data = data;
  t->done = xhw_event_create();
  // MusyX's mixer is the only SDL thread: above the game thread, as the
  // GameCube's DSP interrupt was
  if (!xhw_thread_start(thread_entry, t, 1, 64 * 1024)) {
    free(t);
    return nullptr;
  }
  xhw_logf("[SDL] thread %s started", name ? name : "?");
  return t;
}

void px_SDL_WaitThread(SDL_Thread* t, int* status) {
  if (t == nullptr)
    return;
  xhw_event_wait(t->done, 0xFFFFFFFFu);
  if (status)
    *status = t->status;
}

bool px_SDL_SetCurrentThreadPriority(SDL_ThreadPriority) { return true; }

SDL_Mutex* px_SDL_CreateMutex(void) { return reinterpret_cast< SDL_Mutex* >(xhw_mutex_create()); }
void px_SDL_DestroyMutex(SDL_Mutex*) {}
void px_SDL_LockMutex(SDL_Mutex* m) {
  if (m)
    xhw_mutex_lock(reinterpret_cast< xhw_mutex* >(m));
}
void px_SDL_UnlockMutex(SDL_Mutex* m) {
  if (m)
    xhw_mutex_unlock(reinterpret_cast< xhw_mutex* >(m));
}

// ---- MusyX's output: 16-bit stereo at the game's 32 kHz into the AC97 ring ----
#define RING_FRAMES 16384  // xhw_audio.c's ring, in stereo frames

struct SDL_AudioStream {
  int rate;
};
static SDL_AudioStream s_stream;

SDL_AudioStream* px_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec* spec,
                                              SDL_AudioStreamCallback cb, void*) {
  if (spec == nullptr || spec->channels != 2 || spec->format != SDL_AUDIO_S16 || cb != nullptr) {
    xhw_logf("[AUDIO] unsupported stream format");
    return nullptr;
  }
  if (!xhw_audio_init((uint32_t)spec->freq))
    return nullptr;
  s_stream.rate = spec->freq;
  xhw_logf("[AUDIO] MusyX output %d Hz stereo", spec->freq);
  return &s_stream;
}

// the streamed music and movie audio the main loop queued (prime_sdk.c's AI)
extern "C" void px_ai_mix(int16_t* buf, uint32_t frames);

bool px_SDL_PutAudioStreamData(SDL_AudioStream*, const void* buf, int len) {
  static int16_t mix[1024 * 2];
  const int16_t* in = static_cast< const int16_t* >(buf);
  uint32_t frames = (uint32_t)len / 4u;
  while (frames) {
    const uint32_t n = frames < 1024u ? frames : 1024u;
    memcpy(mix, in, n * 4u);
    px_ai_mix(mix, n);
    xhw_audio_write(mix, n);
    in += n * 2;
    frames -= n;
  }
  return true;
}

int px_SDL_GetAudioStreamQueued(SDL_AudioStream*) { return (int)((RING_FRAMES - xhw_audio_space()) * 4u); }
bool px_SDL_ResumeAudioStreamDevice(SDL_AudioStream*) { return true; }
void px_SDL_DestroyAudioStream(SDL_AudioStream*) { xhw_audio_stop(); }
const char* px_SDL_GetCurrentAudioDriver(void) { return "ac97"; }

// ---- message boxes: the log ----
bool px_SDL_ShowMessageBox(const SDL_MessageBoxData* data, int* buttonid) {
  xhw_logf("[MSGBOX] %s: %s", data && data->title ? data->title : "", data && data->message ? data->message : "");
  // the default (escape) answer: never act on the user's behalf
  if (buttonid != nullptr && data != nullptr) {
    *buttonid = -1;
    for (int i = 0; i < data->numbuttons; ++i)
      if (data->buttons[i].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT)
        *buttonid = data->buttons[i].buttonID;
  }
  return true;
}

bool px_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags, const char* title, const char* message, SDL_Window*) {
  xhw_logf("[MSGBOX] %s: %s", title ? title : "", message ? message : "");
  return true;
}

}  // extern "C"
