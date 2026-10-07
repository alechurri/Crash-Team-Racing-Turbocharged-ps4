// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS4 implementation of the SDL3 functions Crash Team Racing: Turbocharged calls (ps4/ps4.cmake
// explains why this exists instead of an SDL backend). Only what the game uses, with SDL3's
// documented behaviour for it; the game is compiled against SDL's own public headers.
//
//   graphics  Mesa's EGL "orbis" platform; desktop GL (the game's PC path) via eglGetProcAddress
//   input     DualShock 4 through scePad, presented as one SDL gamepad
//   audio     the game's 44.1 kHz stereo stream, resampled to the 48 kHz sceAudioOut port
//   files     everything under /data/ctr/ (the base path and the anchor of relative paths);
//             the read-only assets shipped in the package (/app0/assets) are copied there once

#include <SDL3/SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <orbis/Pad.h>

#include "ps4/ps4_platform.h"

// ---- console APIs (declared here: the SDK headers disagree with each other on a few types) ----
int32_t sceUserServiceInitialize(void *params);
int32_t sceUserServiceGetInitialUser(int32_t *user);
int32_t sceSystemServiceHideSplashScreen(void);
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t frames, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *samples);
int32_t sceAudioOutClose(int32_t handle);
uint32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, void *res);
const char *sceKernelGetFsSandboxRandomWord(void);
void orbis_set_anchor_root(const char *path); // orbis-compat: root of every relative path

#define PS4_DATA_DIR    "/data/ctr/"
#define PS4_PACKAGE_DIR "/app0/assets"
#define PS4_SCREEN_W    1920
#define PS4_SCREEN_H    1080

// ---------------------------------------------------------------------------------------------
// Diagnostics go to /data/ctr/ps4.log (ps4/ps4_platform.c).
#define Ps4_Log CtrPs4_Log

// ---------------------------------------------------------------------------------------------
// Errors, hints, memory

static char s_error[512];

bool SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(s_error, sizeof(s_error), fmt, args);
	va_end(args);
	return false;
}

const char *SDL_GetError(void)
{
	return s_error;
}

void SDL_free(void *mem)
{
	free(mem);
}

int SDL_memcmp(const void *s1, const void *s2, size_t len)
{
	return memcmp(s1, s2, len);
}

#define PS4_MAX_HINTS 64
static struct
{
	char *name;
	char *value;
} s_hints[PS4_MAX_HINTS];

bool SDL_SetHint(const char *name, const char *value)
{
	if (name == NULL)
	{
		return false;
	}
	for (int i = 0; i < PS4_MAX_HINTS; i++)
	{
		if (s_hints[i].name != NULL && strcmp(s_hints[i].name, name) == 0)
		{
			free(s_hints[i].value);
			s_hints[i].value = value ? strdup(value) : NULL;
			return true;
		}
	}
	for (int i = 0; i < PS4_MAX_HINTS; i++)
	{
		if (s_hints[i].name == NULL)
		{
			s_hints[i].name = strdup(name);
			s_hints[i].value = value ? strdup(value) : NULL;
			return true;
		}
	}
	return false;
}

const char *SDL_GetHint(const char *name)
{
	for (int i = 0; name != NULL && i < PS4_MAX_HINTS; i++)
	{
		if (s_hints[i].name != NULL && strcmp(s_hints[i].name, name) == 0)
		{
			return s_hints[i].value;
		}
	}
	return NULL;
}

// ---------------------------------------------------------------------------------------------
// Time

static Uint64 Ps4_NowNS(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}

static Uint64 s_startNS;

Uint64 SDL_GetTicksNS(void)
{
	if (s_startNS == 0)
	{
		s_startNS = Ps4_NowNS();
	}
	return Ps4_NowNS() - s_startNS;
}

Uint64 SDL_GetTicks(void)
{
	return SDL_GetTicksNS() / 1000000ull;
}

Uint64 SDL_GetPerformanceCounter(void)
{
	return Ps4_NowNS();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
	return 1000000000ull;
}

void SDL_DelayPrecise(Uint64 ns)
{
	const Uint64 end = Ps4_NowNS() + ns;
	// Sleep for most of it, then spin the last stretch (the scheduler's granularity is ~1 ms).
	if (ns > 2000000ull)
	{
		struct timespec ts = {(time_t)((ns - 1000000ull) / 1000000000ull), (long)((ns - 1000000ull) % 1000000000ull)};
		nanosleep(&ts, NULL);
	}
	while (Ps4_NowNS() < end)
	{
	}
}

void SDL_Delay(Uint32 ms)
{
	struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
	nanosleep(&ts, NULL);
}

// ---------------------------------------------------------------------------------------------
// Atomics, mutexes, conditions, threads

int SDL_GetAtomicInt(SDL_AtomicInt *a)
{
	return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

int SDL_SetAtomicInt(SDL_AtomicInt *a, int v)
{
	return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST);
}

struct SDL_Mutex
{
	pthread_mutex_t mutex;
};

SDL_Mutex *SDL_CreateMutex(void)
{
	SDL_Mutex *m = calloc(1, sizeof(*m));
	if (m == NULL)
	{
		return NULL;
	}
	// SDL mutexes are recursive.
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return m;
}

void SDL_LockMutex(SDL_Mutex *mutex)
{
	if (mutex != NULL)
	{
		pthread_mutex_lock(&mutex->mutex);
	}
}

void SDL_UnlockMutex(SDL_Mutex *mutex)
{
	if (mutex != NULL)
	{
		pthread_mutex_unlock(&mutex->mutex);
	}
}

void SDL_DestroyMutex(SDL_Mutex *mutex)
{
	if (mutex != NULL)
	{
		pthread_mutex_destroy(&mutex->mutex);
		free(mutex);
	}
}

struct SDL_Condition
{
	pthread_cond_t cond;
};

SDL_Condition *SDL_CreateCondition(void)
{
	SDL_Condition *c = calloc(1, sizeof(*c));
	if (c != NULL)
	{
		pthread_cond_init(&c->cond, NULL);
	}
	return c;
}

void SDL_SignalCondition(SDL_Condition *cond)
{
	if (cond != NULL)
	{
		pthread_cond_signal(&cond->cond);
	}
}

void SDL_WaitCondition(SDL_Condition *cond, SDL_Mutex *mutex)
{
	if (cond != NULL && mutex != NULL)
	{
		pthread_cond_wait(&cond->cond, &mutex->mutex);
	}
}

void SDL_DestroyCondition(SDL_Condition *cond)
{
	if (cond != NULL)
	{
		pthread_cond_destroy(&cond->cond);
		free(cond);
	}
}

struct SDL_Thread
{
	pthread_t thread;
	SDL_ThreadFunction fn;
	void *data;
	int status;
	SDL_AtomicInt state;
};

static void *Ps4_ThreadEntry(void *arg)
{
	SDL_Thread *t = arg;
	t->status = t->fn(t->data);
	SDL_SetAtomicInt(&t->state, SDL_THREAD_COMPLETE);
	return NULL;
}

SDL_Thread *SDL_CreateThreadRuntime(SDL_ThreadFunction fn, const char *name, void *data, SDL_FunctionPointer pfnBeginThread,
                                    SDL_FunctionPointer pfnEndThread)
{
	(void)pfnBeginThread;
	(void)pfnEndThread;
	SDL_Thread *t = calloc(1, sizeof(*t));
	if (t == NULL)
	{
		return NULL;
	}
	t->fn = fn;
	t->data = data;
	SDL_SetAtomicInt(&t->state, SDL_THREAD_ALIVE);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
	const int rc = pthread_create(&t->thread, &attr, Ps4_ThreadEntry, t);
	pthread_attr_destroy(&attr);
	if (rc != 0)
	{
		Ps4_Log("thread '%s': pthread_create failed (%d)", name ? name : "?", rc);
		free(t);
		SDL_SetError("pthread_create failed");
		return NULL;
	}
	return t;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	if (thread == NULL)
	{
		return;
	}
	pthread_join(thread->thread, NULL);
	if (status != NULL)
	{
		*status = thread->status;
	}
	free(thread);
}

SDL_ThreadState SDL_GetThreadState(SDL_Thread *thread)
{
	return thread ? (SDL_ThreadState)SDL_GetAtomicInt(&thread->state) : SDL_THREAD_UNKNOWN;
}

bool SDL_SetCurrentThreadPriority(SDL_ThreadPriority priority)
{
	(void)priority;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Files and paths

struct SDL_IOStream
{
	FILE *file;
};

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
	FILE *f = (file && mode) ? fopen(file, mode) : NULL;
	if (f == NULL)
	{
		SDL_SetError("Couldn't open %s: %s", file ? file : "(null)", strerror(errno));
		return NULL;
	}
	SDL_IOStream *io = calloc(1, sizeof(*io));
	if (io == NULL)
	{
		fclose(f);
		return NULL;
	}
	io->file = f;
	return io;
}

size_t SDL_ReadIO(SDL_IOStream *context, void *ptr, size_t size)
{
	return context ? fread(ptr, 1, size, context->file) : 0;
}

Sint64 SDL_SeekIO(SDL_IOStream *context, Sint64 offset, SDL_IOWhence whence)
{
	if (context == NULL)
	{
		return -1;
	}
	const int origin = whence == SDL_IO_SEEK_CUR ? SEEK_CUR : whence == SDL_IO_SEEK_END ? SEEK_END : SEEK_SET;
	if (fseeko(context->file, (off_t)offset, origin) != 0)
	{
		return -1;
	}
	return (Sint64)ftello(context->file);
}

Sint64 SDL_GetIOSize(SDL_IOStream *context)
{
	if (context == NULL)
	{
		return -1;
	}
	struct stat st;
	if (fstat(fileno(context->file), &st) != 0)
	{
		return -1;
	}
	return (Sint64)st.st_size;
}

bool SDL_CloseIO(SDL_IOStream *context)
{
	if (context == NULL)
	{
		return true;
	}
	const bool ok = fclose(context->file) == 0;
	free(context);
	return ok;
}

bool SDL_CreateDirectory(const char *path)
{
	if (path == NULL || path[0] == '\0')
	{
		return SDL_SetError("empty path");
	}
	// Like SDL3: create the missing parents too, and succeed if it already exists.
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s", path);
	for (char *p = buf + 1; *p != '\0'; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			mkdir(buf, 0777);
			*p = '/';
		}
	}
	if (mkdir(buf, 0777) != 0 && errno != EEXIST)
	{
		return SDL_SetError("mkdir %s: %s", path, strerror(errno));
	}
	return true;
}

bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct stat st;
	if (path == NULL || stat(path, &st) != 0)
	{
		if (info != NULL)
		{
			memset(info, 0, sizeof(*info));
		}
		return SDL_SetError("Can't stat %s", path ? path : "(null)");
	}
	if (info != NULL)
	{
		info->type = S_ISREG(st.st_mode) ? SDL_PATHTYPE_FILE : S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_OTHER;
		info->size = (Uint64)st.st_size;
		info->create_time = (SDL_Time)st.st_ctime * 1000000000ll;
		info->modify_time = (SDL_Time)st.st_mtime * 1000000000ll;
		info->access_time = (SDL_Time)st.st_atime * 1000000000ll;
	}
	return true;
}

bool SDL_RemovePath(const char *path)
{
	if (path == NULL)
	{
		return false;
	}
	if (remove(path) == 0 || errno == ENOENT)
	{
		return true;
	}
	if (rmdir(path) == 0)
	{
		return true;
	}
	return SDL_SetError("remove %s: %s", path, strerror(errno));
}

bool SDL_RenamePath(const char *oldpath, const char *newpath)
{
	if (oldpath == NULL || newpath == NULL || rename(oldpath, newpath) != 0)
	{
		return SDL_SetError("rename failed: %s", strerror(errno));
	}
	return true;
}

static void Ps4_CopyFile(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb");
	if (in == NULL)
	{
		return;
	}
	FILE *out = fopen(to, "wb");
	if (out == NULL)
	{
		fclose(in);
		Ps4_Log("assets: cannot write %s", to);
		return;
	}
	char buf[64 * 1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
	{
		fwrite(buf, 1, n, out);
	}
	fclose(in);
	fclose(out);
}

// Copies the package's assets (fonts, the controller picture, licences) into the writable data
// directory, without replacing what is there (the user's disc image and fonts live there too).
static void Ps4_InstallAssets(const char *from, const char *to)
{
	DIR *dir = opendir(from);
	if (dir == NULL)
	{
		return;
	}
	SDL_CreateDirectory(to);
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_name[0] == '.')
		{
			continue;
		}
		char src[1024], dst[1024];
		snprintf(src, sizeof(src), "%s/%s", from, entry->d_name);
		snprintf(dst, sizeof(dst), "%s/%s", to, entry->d_name);
		struct stat st;
		if (stat(src, &st) != 0)
		{
			continue;
		}
		if (S_ISDIR(st.st_mode))
		{
			Ps4_InstallAssets(src, dst);
		}
		else if (stat(dst, &st) != 0)
		{
			Ps4_CopyFile(src, dst);
		}
	}
	closedir(dir);
}

const char *SDL_GetBasePath(void)
{
	static int ready;
	if (!ready)
	{
		ready = 1;
		mkdir(PS4_DATA_DIR, 0777);
		orbis_set_anchor_root(PS4_DATA_DIR);
		Ps4_InstallAssets(PS4_PACKAGE_DIR, PS4_DATA_DIR "assets");
		struct stat st;
		Ps4_Log("base path %s; disc image %s", PS4_DATA_DIR,
		        stat(PS4_DATA_DIR "assets/ctr-u.bin", &st) == 0 ? "present" : "MISSING (put your raw BIN dump at /data/ctr/assets/ctr-u.bin)");
	}
	return PS4_DATA_DIR;
}

// ---------------------------------------------------------------------------------------------
// Init

static int s_padHandle = -1;
static int s_userId = -1;

static void Ps4_LoadSystemModules(void)
{
	const char *word = sceKernelGetFsSandboxRandomWord();
	static const char *const names[] = {"libSceSysCore", "libSceMbus", "libSceIpmi", "libSceSystemService",
	                                    "libSceUserService", "libSceAudioOut", "libScePad"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
	{
		char path[256];
		snprintf(path, sizeof(path), "/%s/common/lib/%s.sprx", word ? word : "system", names[i]);
		int32_t result = 0;
		sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &result);
	}
}

static void Ps4_InitPlatform(void)
{
	static int done;
	if (done)
	{
		return;
	}
	done = 1;
	Ps4_LoadSystemModules();
	struct
	{
		int32_t priority;
	} params = {700};
	sceUserServiceInitialize(&params);
	if (sceUserServiceGetInitialUser(&s_userId) < 0)
	{
		Ps4_Log("sceUserServiceGetInitialUser failed");
	}
	const int32_t padInit = scePadInit();
	s_padHandle = scePadOpen(s_userId, 0 /* standard */, 0, NULL);
	if (s_padHandle < 0)
	{
		s_padHandle = scePadGetHandle(s_userId, 0, 0);
	}
	Ps4_Log("user %d, scePadInit 0x%08x, pad handle %d", s_userId, (unsigned)padInit, s_padHandle);
	sceSystemServiceHideSplashScreen();
}

bool SDL_Init(SDL_InitFlags flags)
{
	(void)flags;
	SDL_GetBasePath();
	Ps4_InitPlatform();
	return true;
}

bool SDL_InitSubSystem(SDL_InitFlags flags)
{
	return SDL_Init(flags);
}

void SDL_QuitSubSystem(SDL_InitFlags flags)
{
	(void)flags;
}

void SDL_Quit(void)
{
}

// ---------------------------------------------------------------------------------------------
// Gamepad: the DualShock 4 as SDL gamepad 1

#define PS4_GAMEPAD_ID ((SDL_JoystickID)1)

struct SDL_Joystick
{
	int unused;
};
struct SDL_Gamepad
{
	int open;
};

static struct SDL_Joystick s_joystick;
static struct SDL_Gamepad s_gamepad;
static OrbisPadData s_pad;
static Uint64 s_padReadNS;
static Uint64 s_rumbleUntilNS;

static void Ps4_ReadPad(void)
{
	const Uint64 now = Ps4_NowNS();
	if (s_padHandle >= 0 && now - s_padReadNS > 2000000ull)
	{
		s_padReadNS = now;
		OrbisPadData data;
		memset(&data, 0, sizeof(data));
		if (scePadReadState(s_padHandle, &data) >= 0)
		{
			s_pad = data;
		}
	}
	if (s_rumbleUntilNS != 0 && now >= s_rumbleUntilNS)
	{
		s_rumbleUntilNS = 0;
		OrbisPadVibeParam stop = {0, 0};
		scePadSetVibration(s_padHandle, &stop);
	}
}

SDL_JoystickID *SDL_GetGamepads(int *count)
{
	Ps4_InitPlatform();
	SDL_JoystickID *ids = calloc(2, sizeof(*ids));
	int n = 0;
	if (ids != NULL && s_padHandle >= 0)
	{
		ids[n++] = PS4_GAMEPAD_ID;
	}
	if (count != NULL)
	{
		*count = n;
	}
	return ids;
}

bool SDL_IsGamepad(SDL_JoystickID instance_id)
{
	return instance_id == PS4_GAMEPAD_ID && s_padHandle >= 0;
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID instance_id)
{
	if (!SDL_IsGamepad(instance_id))
	{
		SDL_SetError("no such gamepad");
		return NULL;
	}
	s_gamepad.open = 1;
	return &s_gamepad;
}

void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
	if (gamepad != NULL)
	{
		gamepad->open = 0;
	}
}

bool SDL_GamepadConnected(SDL_Gamepad *gamepad)
{
	return gamepad != NULL && s_padHandle >= 0;
}

SDL_Joystick *SDL_GetGamepadJoystick(SDL_Gamepad *gamepad)
{
	return gamepad != NULL ? &s_joystick : NULL;
}

SDL_JoystickID SDL_GetJoystickID(SDL_Joystick *joystick)
{
	return joystick != NULL ? PS4_GAMEPAD_ID : 0;
}

bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	if (gamepad == NULL)
	{
		return false;
	}
	Ps4_ReadPad();
	uint32_t mask = 0;
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_SOUTH: mask = ORBIS_PAD_BUTTON_CROSS; break;
	case SDL_GAMEPAD_BUTTON_EAST: mask = ORBIS_PAD_BUTTON_CIRCLE; break;
	case SDL_GAMEPAD_BUTTON_WEST: mask = ORBIS_PAD_BUTTON_SQUARE; break;
	case SDL_GAMEPAD_BUTTON_NORTH: mask = ORBIS_PAD_BUTTON_TRIANGLE; break;
	// The Share button belongs to the system; the touch pad click is the PS4's Back/Select.
	case SDL_GAMEPAD_BUTTON_BACK: mask = ORBIS_PAD_BUTTON_TOUCH_PAD; break;
	case SDL_GAMEPAD_BUTTON_TOUCHPAD: mask = ORBIS_PAD_BUTTON_TOUCH_PAD; break;
	case SDL_GAMEPAD_BUTTON_START: mask = ORBIS_PAD_BUTTON_OPTIONS; break;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK: mask = ORBIS_PAD_BUTTON_L3; break;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK: mask = ORBIS_PAD_BUTTON_R3; break;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: mask = ORBIS_PAD_BUTTON_L1; break;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: mask = ORBIS_PAD_BUTTON_R1; break;
	case SDL_GAMEPAD_BUTTON_DPAD_UP: mask = ORBIS_PAD_BUTTON_UP; break;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: mask = ORBIS_PAD_BUTTON_DOWN; break;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: mask = ORBIS_PAD_BUTTON_LEFT; break;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: mask = ORBIS_PAD_BUTTON_RIGHT; break;
	default: return false;
	}
	return s_pad.connected && (s_pad.buttons & mask) != 0;
}

static Sint16 Ps4_StickAxis(uint8_t v)
{
	// 0..255 with 128 at rest -> -32768..32767, up/left negative as in SDL.
	const int centered = (int)v - 128;
	return (Sint16)(centered >= 0 ? centered * 32767 / 127 : centered * 256);
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	if (gamepad == NULL)
	{
		return 0;
	}
	Ps4_ReadPad();
	if (!s_pad.connected)
	{
		return 0;
	}
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFTX: return Ps4_StickAxis(s_pad.leftStick.x);
	case SDL_GAMEPAD_AXIS_LEFTY: return Ps4_StickAxis(s_pad.leftStick.y);
	case SDL_GAMEPAD_AXIS_RIGHTX: return Ps4_StickAxis(s_pad.rightStick.x);
	case SDL_GAMEPAD_AXIS_RIGHTY: return Ps4_StickAxis(s_pad.rightStick.y);
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return (Sint16)(s_pad.analogButtons.l2 * 32767 / 255);
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return (Sint16)(s_pad.analogButtons.r2 * 32767 / 255);
	default: return 0;
	}
}

bool SDL_RumbleGamepad(SDL_Gamepad *gamepad, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms)
{
	if (gamepad == NULL || s_padHandle < 0)
	{
		return false;
	}
	OrbisPadVibeParam vibe = {(uint8_t)(low_frequency_rumble >> 8), (uint8_t)(high_frequency_rumble >> 8)};
	scePadSetVibration(s_padHandle, &vibe);
	s_rumbleUntilNS = (vibe.lgMotor || vibe.smMotor) ? Ps4_NowNS() + (Uint64)duration_ms * 1000000ull : 0;
	return true;
}

int SDL_AddGamepadMappingsFromFile(const char *file)
{
	(void)file;
	return 0;
}

// SDL3's mapping-string names.
const char *SDL_GetGamepadStringForButton(SDL_GamepadButton button)
{
	static const char *const names[SDL_GAMEPAD_BUTTON_COUNT] = {
	    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
	    "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
	    "misc2", "misc3", "misc4", "misc5", "misc6"};
	return (button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT && names[button]) ? names[button] : NULL;
}

const char *SDL_GetGamepadStringForAxis(SDL_GamepadAxis axis)
{
	static const char *const names[SDL_GAMEPAD_AXIS_COUNT] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
	return (axis >= 0 && axis < SDL_GAMEPAD_AXIS_COUNT) ? names[axis] : NULL;
}

// ---------------------------------------------------------------------------------------------
// Events, keyboard, mouse (no keyboard or mouse on this platform)

void SDL_PumpEvents(void)
{
	Ps4_ReadPad();
}

bool SDL_PollEvent(SDL_Event *event)
{
	(void)event;
	Ps4_ReadPad();
	return false;
}

const bool *SDL_GetKeyboardState(int *numkeys)
{
	static bool keys[SDL_SCANCODE_COUNT];
	if (numkeys != NULL)
	{
		*numkeys = SDL_SCANCODE_COUNT;
	}
	return keys;
}

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
	(void)scancode;
	return "";
}

SDL_MouseButtonFlags SDL_GetMouseState(float *x, float *y)
{
	if (x != NULL)
	{
		*x = 0.0f;
	}
	if (y != NULL)
	{
		*y = 0.0f;
	}
	return 0;
}

bool SDL_HideCursor(void)
{
	return true;
}

bool SDL_ShowCursor(void)
{
	return true;
}

// ---------------------------------------------------------------------------------------------
// Window and OpenGL (EGL on Mesa's orbis platform)
//
// Facts from orbis-ports' SDL2 driver and RetroArch context (both run on this console): the
// default display is the orbis platform; the native window handle must be non-NULL and is
// otherwise ignored; there is one window surface per process, always the scan-out size; the swap
// interval is fixed at 1 (vsync flip); desktop GL and GLES both work, desktop entry points through
// eglGetProcAddress.

struct SDL_Window
{
	SDL_WindowFlags flags;
};

static struct SDL_Window s_window;
static int s_windowCreated;
static EGLDisplay s_eglDisplay = EGL_NO_DISPLAY;
static EGLSurface s_eglSurface = EGL_NO_SURFACE;
static EGLConfig s_eglConfig;
static EGLContext s_eglContext = EGL_NO_CONTEXT;
static int s_glMajor = 3, s_glMinor = 3, s_glProfile = SDL_GL_CONTEXT_PROFILE_CORE;
static int s_glDepth = 24, s_glStencil = 8;

void *CtrPs4_GLGetProcAddress(const char *name)
{
	return (void *)eglGetProcAddress(name);
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
	(void)title;
	(void)w;
	(void)h;
	if (s_windowCreated)
	{
		SDL_SetError("one window only on this platform");
		return NULL;
	}
	s_windowCreated = 1;
	s_window.flags = flags | SDL_WINDOW_FULLSCREEN;
	return &s_window;
}

void SDL_DestroyWindow(SDL_Window *window)
{
	(void)window;
	// The surface and context stay: the console allows one window surface per process.
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
	return window ? window->flags : 0;
}

bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{
	(void)window;
	if (w != NULL)
	{
		*w = PS4_SCREEN_W;
	}
	if (h != NULL)
	{
		*h = PS4_SCREEN_H;
	}
	return true;
}

bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
	(void)window;
	(void)fullscreen;
	return true;
}

bool SDL_SetWindowFullscreenMode(SDL_Window *window, const SDL_DisplayMode *mode)
{
	(void)window;
	(void)mode;
	return true;
}

bool SDL_SyncWindow(SDL_Window *window)
{
	(void)window;
	return true;
}

bool SDL_GL_SetAttribute(SDL_GLAttr attr, int value)
{
	switch (attr)
	{
	case SDL_GL_CONTEXT_MAJOR_VERSION: s_glMajor = value; break;
	case SDL_GL_CONTEXT_MINOR_VERSION: s_glMinor = value; break;
	case SDL_GL_CONTEXT_PROFILE_MASK: s_glProfile = value; break;
	case SDL_GL_DEPTH_SIZE: s_glDepth = value; break;
	case SDL_GL_STENCIL_SIZE: s_glStencil = value > 0 ? 8 : 0; break;
	default: break;
	}
	return true;
}

static int Ps4_InitEGL(void)
{
	if (s_eglSurface != EGL_NO_SURFACE)
	{
		return 1;
	}
	s_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (s_eglDisplay == EGL_NO_DISPLAY || !eglInitialize(s_eglDisplay, &major, &minor))
	{
		Ps4_Log("EGL: initialize failed (0x%x)", eglGetError());
		return 0;
	}
	Ps4_Log("EGL %d.%d, vendor \"%s\", client APIs \"%s\"", major, minor, eglQueryString(s_eglDisplay, EGL_VENDOR),
	        eglQueryString(s_eglDisplay, EGL_CLIENT_APIS));
	const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
	                                EGL_WINDOW_BIT,
	                                EGL_RENDERABLE_TYPE,
	                                EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT,
	                                EGL_RED_SIZE,
	                                8,
	                                EGL_GREEN_SIZE,
	                                8,
	                                EGL_BLUE_SIZE,
	                                8,
	                                EGL_ALPHA_SIZE,
	                                8,
	                                EGL_DEPTH_SIZE,
	                                s_glDepth,
	                                EGL_STENCIL_SIZE,
	                                s_glStencil,
	                                EGL_NONE};
	EGLint count = 0;
	if (!eglChooseConfig(s_eglDisplay, configAttribs, &s_eglConfig, 1, &count) || count < 1)
	{
		Ps4_Log("EGL: no config for RGBA8 depth %d stencil %d (0x%x)", s_glDepth, s_glStencil, eglGetError());
		return 0;
	}
	s_eglSurface = eglCreateWindowSurface(s_eglDisplay, s_eglConfig, (EGLNativeWindowType)1, NULL);
	if (s_eglSurface == EGL_NO_SURFACE)
	{
		Ps4_Log("EGL: window surface failed (0x%x)", eglGetError());
		return 0;
	}
	return 1;
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window)
{
	(void)window;
	if (!Ps4_InitEGL())
	{
		SDL_SetError("EGL initialisation failed");
		return NULL;
	}
	const int es = s_glProfile == SDL_GL_CONTEXT_PROFILE_ES;
	eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
	EGLint attribs[16];
	int n = 0;
	attribs[n++] = EGL_CONTEXT_MAJOR_VERSION;
	attribs[n++] = s_glMajor;
	attribs[n++] = EGL_CONTEXT_MINOR_VERSION;
	attribs[n++] = s_glMinor;
	if (!es)
	{
		attribs[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
		attribs[n++] = s_glProfile == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY ? EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
		                                                                   : EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
	}
	attribs[n++] = EGL_NONE;
	EGLContext context = eglCreateContext(s_eglDisplay, s_eglConfig, EGL_NO_CONTEXT, attribs);
	if (context == EGL_NO_CONTEXT)
	{
		Ps4_Log("EGL: %s %d.%d context refused (0x%x)", es ? "GLES" : "GL", s_glMajor, s_glMinor, eglGetError());
		SDL_SetError("context refused");
		return NULL;
	}
	if (!eglMakeCurrent(s_eglDisplay, s_eglSurface, s_eglSurface, context))
	{
		Ps4_Log("EGL: make current failed (0x%x)", eglGetError());
		eglDestroyContext(s_eglDisplay, context);
		return NULL;
	}
	eglSwapInterval(s_eglDisplay, 1);
	s_eglContext = context;
	typedef const unsigned char *(*GetStringFn)(unsigned int);
	GetStringFn getString = (GetStringFn)eglGetProcAddress("glGetString");
	Ps4_Log("GL context %d.%d %s: \"%s\" / \"%s\"", s_glMajor, s_glMinor, es ? "ES" : "core",
	        getString ? (const char *)getString(0x1F02 /* GL_VERSION */) : "?",
	        getString ? (const char *)getString(0x1F01 /* GL_RENDERER */) : "?");
	return (SDL_GLContext)context;
}

bool SDL_GL_SwapWindow(SDL_Window *window)
{
	(void)window;
	if (s_eglSurface == EGL_NO_SURFACE)
	{
		return false;
	}
	CtrPs4_NoteFrame();
	return eglSwapBuffers(s_eglDisplay, s_eglSurface) == EGL_TRUE;
}

bool SDL_GL_SetSwapInterval(int interval)
{
	(void)interval; // fixed at 1 on this platform
	return true;
}

bool SDL_GL_ExtensionSupported(const char *extension)
{
	if (extension == NULL || s_eglContext == EGL_NO_CONTEXT)
	{
		return false;
	}
	typedef void (*GetIntegervFn)(unsigned int, int *);
	typedef const unsigned char *(*GetStringiFn)(unsigned int, unsigned int);
	GetIntegervFn getIntegerv = (GetIntegervFn)eglGetProcAddress("glGetIntegerv");
	GetStringiFn getStringi = (GetStringiFn)eglGetProcAddress("glGetStringi");
	if (getIntegerv == NULL || getStringi == NULL)
	{
		return false;
	}
	int count = 0;
	getIntegerv(0x821D /* GL_NUM_EXTENSIONS */, &count);
	for (int i = 0; i < count; i++)
	{
		const char *name = (const char *)getStringi(0x1F03 /* GL_EXTENSIONS */, (unsigned)i);
		if (name != NULL && strcmp(name, extension) == 0)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Surfaces (screenshots only; not offered on this platform)

SDL_Surface *SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void *pixels, int pitch)
{
	SDL_Surface *surface = calloc(1, sizeof(*surface));
	if (surface != NULL)
	{
		surface->w = width;
		surface->h = height;
		surface->format = format;
		surface->pixels = pixels;
		surface->pitch = pitch;
		surface->refcount = 1;
	}
	return surface;
}

void SDL_DestroySurface(SDL_Surface *surface)
{
	free(surface);
}

bool SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
	(void)surface;
	(void)file;
	return SDL_SetError("screenshots are not supported on the PS4");
}

// ---------------------------------------------------------------------------------------------
// Audio: one playback stream with a callback, as the game opens it, played on a 48 kHz port.

#define PS4_AUDIO_RATE   48000
#define PS4_AUDIO_FRAMES 256 // per sceAudioOutOutput call (5.3 ms)

struct SDL_AudioStream
{
	SDL_Mutex *lock;
	SDL_AudioSpec spec;
	SDL_AudioStreamCallback callback;
	void *userdata;
	float gain;
	Sint16 *ring; // queued source frames, interleaved stereo
	int ringFrames;
	int ringCapacity;
	double position; // fractional read position into ring, in source frames
	pthread_t thread;
	int running;
	int port;
};

static int Ps4_AudioReserve(SDL_AudioStream *s, int frames)
{
	if (frames <= s->ringCapacity)
	{
		return 1;
	}
	int capacity = s->ringCapacity ? s->ringCapacity : 4096;
	while (capacity < frames)
	{
		capacity *= 2;
	}
	Sint16 *ring = realloc(s->ring, (size_t)capacity * 2 * sizeof(Sint16));
	if (ring == NULL)
	{
		return 0;
	}
	s->ring = ring;
	s->ringCapacity = capacity;
	return 1;
}

bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len)
{
	if (stream == NULL || buf == NULL || len < 0)
	{
		return false;
	}
	const int frames = len / (int)(2 * sizeof(Sint16));
	SDL_LockMutex(stream->lock);
	const int ok = Ps4_AudioReserve(stream, stream->ringFrames + frames);
	if (ok)
	{
		memcpy(stream->ring + (size_t)stream->ringFrames * 2, buf, (size_t)frames * 2 * sizeof(Sint16));
		stream->ringFrames += frames;
	}
	SDL_UnlockMutex(stream->lock);
	return ok != 0;
}

int SDL_GetAudioStreamQueued(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return -1;
	}
	SDL_LockMutex(stream->lock);
	const int queued = (stream->ringFrames - (int)stream->position) * (int)(2 * sizeof(Sint16));
	SDL_UnlockMutex(stream->lock);
	return queued > 0 ? queued : 0;
}

bool SDL_ClearAudioStream(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return false;
	}
	SDL_LockMutex(stream->lock);
	stream->ringFrames = 0;
	stream->position = 0.0;
	SDL_UnlockMutex(stream->lock);
	return true;
}

bool SDL_LockAudioStream(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return false;
	}
	SDL_LockMutex(stream->lock);
	return true;
}

bool SDL_UnlockAudioStream(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return false;
	}
	SDL_UnlockMutex(stream->lock);
	return true;
}

bool SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain)
{
	if (stream == NULL)
	{
		return false;
	}
	stream->gain = gain;
	return true;
}

static void *Ps4_AudioThread(void *arg)
{
	SDL_AudioStream *s = arg;
	const double step = (double)s->spec.freq / (double)PS4_AUDIO_RATE; // source frames per output frame
	static Sint16 out[PS4_AUDIO_FRAMES * 2];
	while (s->running)
	{
		SDL_LockMutex(s->lock);
		// Enough source for this block plus the interpolation partner of its last frame.
		const int needed = (int)(s->position + step * PS4_AUDIO_FRAMES) + 2;
		if (s->ringFrames < needed && s->callback != NULL)
		{
			const int missing = (needed - s->ringFrames) * (int)(2 * sizeof(Sint16));
			s->callback(s->userdata, s, missing, missing);
		}
		const float gain = s->gain;
		for (int i = 0; i < PS4_AUDIO_FRAMES; i++)
		{
			const int i0 = (int)s->position;
			const float t = (float)(s->position - (double)i0);
			for (int c = 0; c < 2; c++)
			{
				float a = 0.0f, b = 0.0f;
				if (i0 < s->ringFrames)
				{
					a = s->ring[i0 * 2 + c];
					b = (i0 + 1 < s->ringFrames) ? s->ring[(i0 + 1) * 2 + c] : a;
				}
				float v = (a + (b - a) * t) * gain;
				v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
				out[i * 2 + c] = (Sint16)v;
			}
			s->position += step;
		}
		// Drop the frames consumed; keep the fractional part.
		const int consumed = (int)s->position < s->ringFrames ? (int)s->position : s->ringFrames;
		if (consumed > 0)
		{
			memmove(s->ring, s->ring + (size_t)consumed * 2, (size_t)(s->ringFrames - consumed) * 2 * sizeof(Sint16));
			s->ringFrames -= consumed;
			s->position -= consumed;
			if (s->position < 0.0)
			{
				s->position = 0.0;
			}
		}
		SDL_UnlockMutex(s->lock);
		sceAudioOutOutput(s->port, out); // blocks until the port takes the block: this paces the loop
	}
	return NULL;
}

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec, SDL_AudioStreamCallback callback,
                                           void *userdata)
{
	(void)devid;
	if (spec == NULL || spec->format != SDL_AUDIO_S16 || spec->channels != 2)
	{
		SDL_SetError("only 16-bit stereo streams are supported");
		return NULL;
	}
	Ps4_InitPlatform();
	const int32_t init = sceAudioOutInit();
	if (init != 0 && (uint32_t)init != 0x8026000Eu /* already initialised */)
	{
		Ps4_Log("sceAudioOutInit 0x%08x", (unsigned)init);
	}
	const int port = sceAudioOutOpen(0xFF /* system user */, 0 /* main */, 0, PS4_AUDIO_FRAMES, PS4_AUDIO_RATE, 1 /* S16 stereo */);
	if (port < 0)
	{
		Ps4_Log("sceAudioOutOpen failed 0x%08x", (unsigned)port);
		SDL_SetError("sceAudioOutOpen failed");
		return NULL;
	}
	SDL_AudioStream *s = calloc(1, sizeof(*s));
	if (s == NULL)
	{
		sceAudioOutClose(port);
		return NULL;
	}
	s->lock = SDL_CreateMutex();
	s->spec = *spec;
	s->callback = callback;
	s->userdata = userdata;
	s->gain = 1.0f;
	s->port = port;
	Ps4_Log("audio: port %d, %d Hz stream resampled to %d Hz", port, spec->freq, PS4_AUDIO_RATE);
	return s;
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return false;
	}
	if (stream->running)
	{
		return true;
	}
	stream->running = 1;
	if (pthread_create(&stream->thread, NULL, Ps4_AudioThread, stream) != 0)
	{
		stream->running = 0;
		return SDL_SetError("audio thread failed");
	}
	return true;
}

void SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
	if (stream == NULL)
	{
		return;
	}
	if (stream->running)
	{
		stream->running = 0;
		pthread_join(stream->thread, NULL);
	}
	sceAudioOutClose(stream->port);
	SDL_DestroyMutex(stream->lock);
	free(stream->ring);
	free(stream);
}

SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream *stream)
{
	return stream != NULL ? (SDL_AudioDeviceID)1 : 0;
}

bool SDL_GetAudioStreamFormat(SDL_AudioStream *stream, SDL_AudioSpec *src_spec, SDL_AudioSpec *dst_spec)
{
	if (stream == NULL)
	{
		return false;
	}
	if (src_spec != NULL)
	{
		*src_spec = stream->spec;
	}
	if (dst_spec != NULL)
	{
		dst_spec->format = SDL_AUDIO_S16;
		dst_spec->channels = 2;
		dst_spec->freq = PS4_AUDIO_RATE;
	}
	return true;
}

bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID devid, SDL_AudioSpec *spec, int *sample_frames)
{
	(void)devid;
	if (spec != NULL)
	{
		spec->format = SDL_AUDIO_S16;
		spec->channels = 2;
		spec->freq = PS4_AUDIO_RATE;
	}
	if (sample_frames != NULL)
	{
		*sample_frames = PS4_AUDIO_FRAMES;
	}
	return true;
}

const char *SDL_GetCurrentAudioDriver(void)
{
	return "ps4";
}
