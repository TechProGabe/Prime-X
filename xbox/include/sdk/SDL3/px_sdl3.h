/* px_sdl3.h - the SDL3 subset Prime's game code, MusyX's PC mixer (hw_pc.c)
 * and aurora's event header use, backed by the Xbox (xbox/src/port/px_sdl3.cpp).
 * Every SDL3/<name>.h on the game include path is this file. Only the game
 * sees it; the Xbox layer uses nxdk's SDL2 for the controllers, so the
 * functions are renamed px_SDL_* to keep clear of SDL2's. */
#ifndef PX_SDL3_SHIM_H
#define PX_SDL3_SHIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDLCALL
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;

/* ---- stdinc ---- */
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))
#define SDL_PI_D 3.141592653589793238462643383279502884
#define SDL_PI_F 3.141592653589793238462643383279502884F
#define SDL_fabs fabs
#define SDL_sin sin
#define SDL_cos cos
#define SDL_lround lround
#define SDL_free free
#define SDL_malloc malloc
#define SDL_GetError px_SDL_GetError
const char* px_SDL_GetError(void);

/* ---- init ---- */
#define SDL_INIT_AUDIO 0x10u
#define SDL_InitSubSystem px_SDL_InitSubSystem
#define SDL_QuitSubSystem px_SDL_QuitSubSystem
bool px_SDL_InitSubSystem(Uint32 flags);
void px_SDL_QuitSubSystem(Uint32 flags);

/* ---- timer ---- */
#define SDL_GetTicksNS px_SDL_GetTicksNS
#define SDL_GetTicks px_SDL_GetTicks
#define SDL_Delay px_SDL_Delay
#define SDL_DelayNS px_SDL_DelayNS
#define SDL_DelayPrecise px_SDL_DelayPrecise
Uint64 px_SDL_GetTicksNS(void);
Uint64 px_SDL_GetTicks(void);
void px_SDL_Delay(Uint32 ms);
void px_SDL_DelayNS(Uint64 ns);
void px_SDL_DelayPrecise(Uint64 ns);

/* ---- threads, mutexes, atomics ---- */
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Mutex SDL_Mutex;
typedef int(SDLCALL* SDL_ThreadFunction)(void* data);
typedef enum SDL_ThreadPriority {
  SDL_THREAD_PRIORITY_LOW,
  SDL_THREAD_PRIORITY_NORMAL,
  SDL_THREAD_PRIORITY_HIGH,
  SDL_THREAD_PRIORITY_TIME_CRITICAL
} SDL_ThreadPriority;
typedef struct SDL_AtomicInt {
  volatile int value;
} SDL_AtomicInt;
#define SDL_CreateThread(fn, name, data) px_SDL_CreateThread(fn, name, data)
#define SDL_WaitThread px_SDL_WaitThread
#define SDL_SetCurrentThreadPriority px_SDL_SetCurrentThreadPriority
#define SDL_CreateMutex px_SDL_CreateMutex
#define SDL_DestroyMutex px_SDL_DestroyMutex
#define SDL_LockMutex px_SDL_LockMutex
#define SDL_UnlockMutex px_SDL_UnlockMutex
SDL_Thread* px_SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data);
void px_SDL_WaitThread(SDL_Thread* thread, int* status);
bool px_SDL_SetCurrentThreadPriority(SDL_ThreadPriority priority);
SDL_Mutex* px_SDL_CreateMutex(void);
void px_SDL_DestroyMutex(SDL_Mutex* m);
void px_SDL_LockMutex(SDL_Mutex* m);
void px_SDL_UnlockMutex(SDL_Mutex* m);
static inline int SDL_GetAtomicInt(SDL_AtomicInt* a) { return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST); }
static inline int SDL_SetAtomicInt(SDL_AtomicInt* a, int v) {
  return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST);
}
static inline int SDL_AddAtomicInt(SDL_AtomicInt* a, int v) {
  return __atomic_fetch_add(&a->value, v, __ATOMIC_SEQ_CST);
}

/* ---- audio streams (MusyX's output) ---- */
typedef Uint32 SDL_AudioDeviceID;
typedef enum SDL_AudioFormat { SDL_AUDIO_S16 = 0x8010u, SDL_AUDIO_F32 = 0x8120u } SDL_AudioFormat;
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)
typedef struct SDL_AudioSpec {
  SDL_AudioFormat format;
  int channels;
  int freq;
} SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef void(SDLCALL* SDL_AudioStreamCallback)(void* userdata, SDL_AudioStream* stream,
                                               int additional_amount, int total_amount);
#define SDL_OpenAudioDeviceStream px_SDL_OpenAudioDeviceStream
#define SDL_PutAudioStreamData px_SDL_PutAudioStreamData
#define SDL_GetAudioStreamQueued px_SDL_GetAudioStreamQueued
#define SDL_ResumeAudioStreamDevice px_SDL_ResumeAudioStreamDevice
#define SDL_DestroyAudioStream px_SDL_DestroyAudioStream
#define SDL_GetCurrentAudioDriver px_SDL_GetCurrentAudioDriver
SDL_AudioStream* px_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec* spec,
                                              SDL_AudioStreamCallback cb, void* userdata);
bool px_SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* buf, int len);
int px_SDL_GetAudioStreamQueued(SDL_AudioStream* stream);
bool px_SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream);
void px_SDL_DestroyAudioStream(SDL_AudioStream* stream);
const char* px_SDL_GetCurrentAudioDriver(void);

/* ---- windows, keyboard, mouse, gamepads: none on the Xbox ---- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef Uint32 SDL_JoystickID;
typedef Uint32 SDL_MouseID;
typedef Uint32 SDL_MouseButtonFlags;
typedef int SDL_Scancode;
#define SDL_SCANCODE_F10 67
#define SDL_BUTTON_MASK(X) (1u << ((X)-1))
#define SDL_TOUCH_MOUSEID ((SDL_MouseID)-1)
#define SDL_PEN_MOUSEID ((SDL_MouseID)-2)
typedef enum SDL_GamepadButton {
  SDL_GAMEPAD_BUTTON_INVALID = -1,
  SDL_GAMEPAD_BUTTON_SOUTH,
  SDL_GAMEPAD_BUTTON_EAST,
  SDL_GAMEPAD_BUTTON_WEST,
  SDL_GAMEPAD_BUTTON_NORTH,
  SDL_GAMEPAD_BUTTON_BACK,
  SDL_GAMEPAD_BUTTON_GUIDE,
  SDL_GAMEPAD_BUTTON_START
} SDL_GamepadButton;
static inline SDL_Window** SDL_GetWindows(int* count) {
  if (count)
    *count = 0;
  return NULL;
}
static inline SDL_Window* SDL_GetKeyboardFocus(void) { return NULL; }
static inline SDL_Window* SDL_GetMouseFocus(void) { return NULL; }
static inline const bool* SDL_GetKeyboardState(int* numkeys) {
  if (numkeys)
    *numkeys = 0;
  return NULL;
}
static inline SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y) {
  if (x)
    *x = 0;
  if (y)
    *y = 0;
  return 0;
}
static inline bool SDL_GetWindowRelativeMouseMode(SDL_Window* w) { return false; }
static inline bool SDL_SetWindowRelativeMouseMode(SDL_Window* w, bool on) { return false; }
static inline bool SDL_HideCursor(void) { return true; }
static inline bool SDL_ShowCursor(void) { return true; }
static inline bool SDL_GetGamepadButton(SDL_Gamepad* p, SDL_GamepadButton b) { return false; }

/* ---- events: the Xbox has none to give; the types for aurora/event.h ---- */
enum {
  SDL_EVENT_WINDOW_FOCUS_LOST = 0x20F,
  SDL_EVENT_KEY_DOWN = 0x300,
  SDL_EVENT_MOUSE_MOTION = 0x400,
  SDL_EVENT_MOUSE_BUTTON_DOWN,
  SDL_EVENT_MOUSE_BUTTON_UP,
  SDL_EVENT_MOUSE_REMOVED = 0x405,
  SDL_EVENT_FINGER_UP = 0x701
};
typedef union SDL_Event {
  Uint32 type;
  struct {
    Uint32 type;
    Uint64 timestamp;
    SDL_Scancode scancode;
    bool down, repeat;
  } key;
  struct {
    Uint32 type;
    Uint64 timestamp;
    SDL_MouseID which;
    float x, y, xrel, yrel;
  } motion;
  struct {
    Uint32 type;
    Uint64 timestamp;
    SDL_MouseID which;
    Uint8 button;
    bool down;
  } button;
  struct {
    Uint32 type;
    Uint64 timestamp;
    float x, y;
  } tfinger;
  Uint8 padding[128];
} SDL_Event;

/* ---- message boxes: logged ---- */
typedef Uint32 SDL_MessageBoxFlags;
typedef Uint32 SDL_MessageBoxButtonFlags;
#define SDL_MESSAGEBOX_ERROR 0x10u
#define SDL_MESSAGEBOX_WARNING 0x20u
#define SDL_MESSAGEBOX_INFORMATION 0x40u
#define SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT 0x1u
#define SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT 0x2u
typedef struct SDL_MessageBoxButtonData {
  SDL_MessageBoxButtonFlags flags;
  int buttonID;
  const char* text;
} SDL_MessageBoxButtonData;
typedef struct SDL_MessageBoxData {
  SDL_MessageBoxFlags flags;
  SDL_Window* window;
  const char* title;
  const char* message;
  int numbuttons;
  const SDL_MessageBoxButtonData* buttons;
  const void* colorScheme;
} SDL_MessageBoxData;
#define SDL_ShowMessageBox px_SDL_ShowMessageBox
#define SDL_ShowSimpleMessageBox px_SDL_ShowSimpleMessageBox
bool px_SDL_ShowMessageBox(const SDL_MessageBoxData* data, int* buttonid);
bool px_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message,
                                 SDL_Window* w);

#ifdef __cplusplus
}
#endif
#endif
