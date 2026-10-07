// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS4 platform services: the diagnostics log, crash reports with a backtrace of eboot offsets,
// the orbis-compat/driver log sink, the GPU driver's own log, and a frame counter with a hang
// watchdog. Everything goes to /data/ctr/ps4.log (mesa.log for the driver), so a failed run on the
// console always leaves something to read.

#include "ps4/ps4_platform.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int sceKernelInstallExceptionHandler(int signal, void (*handler)(int, void *));
int sceKernelMkdir(const char *path, int mode);
int sceKernelRmdir(const char *path);
typedef void (*orbis_log_fn)(const char *fmt, va_list ap);
void orbis_set_log(orbis_log_fn fn);
void orbis_set_log_fatal(orbis_log_fn fn);

#ifndef CTR_PS4_BUILD_ID
#define CTR_PS4_BUILD_ID "dev"
#endif

static FILE *s_log;
static pthread_mutex_t s_logLock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t Ps4_Seconds100(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 100u + (uint64_t)ts.tv_nsec / 10000000u;
}

static uint64_t s_t0;

void CtrPs4_LogV(const char *fmt, va_list args)
{
	char line[1024];
	vsnprintf(line, sizeof(line), fmt, args);
	pthread_mutex_lock(&s_logLock);
	if (s_log == NULL)
	{
		mkdir(CTR_PS4_DATA_DIR, 0777);
		// The previous run's log is kept next to the new one.
		rename(CTR_PS4_DATA_DIR "ps4.log", CTR_PS4_DATA_DIR "ps4.old.log");
		s_log = fopen(CTR_PS4_DATA_DIR "ps4.log", "w");
		s_t0 = Ps4_Seconds100();
	}
	if (s_log != NULL)
	{
		const uint64_t t = Ps4_Seconds100() - s_t0;
		fprintf(s_log, "[%5lu.%02lu] %s\n", (unsigned long)(t / 100u), (unsigned long)(t % 100u), line);
		fflush(s_log);
	}
	pthread_mutex_unlock(&s_logLock);
}

void CtrPs4_Log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	CtrPs4_LogV(fmt, args);
	va_end(args);
}

static void Ps4_OrbisLog(const char *fmt, va_list args)
{
	char line[900];
	vsnprintf(line, sizeof(line), fmt, args);
	CtrPs4_Log("  | %s", line);
}

// ---- directories --------------------------------------------------------------------------
// orbis-compat anchors relative paths for open, stat, rename, unlink and remove, but not for mkdir
// or rmdir: a relative mkdir fails with EINVAL. The game creates "memcards/<slot>" relative to its
// base directory, so without these the memory card could never be written ("card full").

static const char *Ps4_Anchor(const char *path, char *buf, size_t size)
{
	if (path == NULL || path[0] == '/')
	{
		return path;
	}
	if (path[0] == '.' && path[1] == '/')
	{
		path += 2;
	}
	snprintf(buf, size, "%s%s", CTR_PS4_DATA_DIR, path);
	return buf;
}

int mkdir(const char *path, mode_t mode)
{
	char buf[1024];
	const int rc = sceKernelMkdir(Ps4_Anchor(path, buf, sizeof(buf)), (int)mode);
	if (rc < 0)
	{
		errno = rc & 0xFFFF; // SCE_KERNEL_ERROR_Exxx = 0x80020000 | errno
		return -1;
	}
	return 0;
}

int rmdir(const char *path)
{
	char buf[1024];
	const int rc = sceKernelRmdir(Ps4_Anchor(path, buf, sizeof(buf)));
	if (rc < 0)
	{
		errno = rc & 0xFFFF;
		return -1;
	}
	return 0;
}

// ---- crash reports ---------------------------------------------------------------------------
// PS4 ucontext: 16-byte signal mask + 48 bytes of padding, then a FreeBSD amd64 mcontext.

#define PS4_IMAGE_BASE 0x400000ull
enum
{
	Mc = 64,
	McRdi = 8, McRsi = 16, McRdx = 24, McRcx = 32, McR8 = 40, McR9 = 48, McRax = 56, McRbx = 64, McRbp = 72,
	McR10 = 80, McR11 = 88, McR12 = 96, McR13 = 104, McR14 = 112, McR15 = 120, McAddr = 136, McErr = 152,
	McRip = 160, McRsp = 184
};

static uint64_t Ps4_Read64(const unsigned char *mc, int offset)
{
	uint64_t v;
	memcpy(&v, mc + offset, sizeof(v));
	return v;
}

static void Ps4_CrashHandler(int signal, void *ucontext)
{
	const unsigned char *mc = (const unsigned char *)ucontext + Mc;
	const uint64_t rip = Ps4_Read64(mc, McRip);
	uint64_t rbp = Ps4_Read64(mc, McRbp);
	const uint64_t rsp = Ps4_Read64(mc, McRsp);
	CtrPs4_Log("!! CRASH signal %d: rip=0x%lx (eboot+0x%lx) fault address=0x%lx rsp=0x%lx err=0x%lx", signal,
	           (unsigned long)rip, (unsigned long)(rip - PS4_IMAGE_BASE), (unsigned long)Ps4_Read64(mc, McAddr), (unsigned long)rsp,
	           (unsigned long)Ps4_Read64(mc, McErr));
	CtrPs4_Log("!! rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx rdi=0x%lx rsi=0x%lx rbp=0x%lx", (unsigned long)Ps4_Read64(mc, McRax),
	           (unsigned long)Ps4_Read64(mc, McRbx), (unsigned long)Ps4_Read64(mc, McRcx), (unsigned long)Ps4_Read64(mc, McRdx),
	           (unsigned long)Ps4_Read64(mc, McRdi), (unsigned long)Ps4_Read64(mc, McRsi), (unsigned long)rbp);
	CtrPs4_Log("!! r8=0x%lx r9=0x%lx r10=0x%lx r11=0x%lx r12=0x%lx r13=0x%lx r14=0x%lx r15=0x%lx", (unsigned long)Ps4_Read64(mc, McR8),
	           (unsigned long)Ps4_Read64(mc, McR9), (unsigned long)Ps4_Read64(mc, McR10), (unsigned long)Ps4_Read64(mc, McR11),
	           (unsigned long)Ps4_Read64(mc, McR12), (unsigned long)Ps4_Read64(mc, McR13), (unsigned long)Ps4_Read64(mc, McR14),
	           (unsigned long)Ps4_Read64(mc, McR15));
	if (rip < 0x10000 && rsp != 0)
	{
		// A call through a null function pointer: the caller's return address is on top.
		CtrPs4_Log("!!   called from eboot+0x%lx (null function pointer)", (unsigned long)(*(const uint64_t *)rsp - PS4_IMAGE_BASE));
	}
	// Frame-pointer walk (the PS4 build keeps frame pointers).
	for (int depth = 0; depth < 24; depth++)
	{
		if (rbp < rsp || rbp - rsp > 8ull * 1024 * 1024 || (rbp & 7) != 0)
		{
			break;
		}
		const uint64_t ret = ((const uint64_t *)rbp)[1];
		CtrPs4_Log("!!   #%02d eboot+0x%lx", depth, (unsigned long)(ret - PS4_IMAGE_BASE));
		const uint64_t next = ((const uint64_t *)rbp)[0];
		if (next <= rbp)
		{
			break;
		}
		rbp = next;
	}
	CtrPs4_Log("!! symbolize with: llvm-symbolizer --obj=ctr_native.elf -C -f 0x<offset>");
	_exit(70);
}

// ---- frames and the hang watchdog -------------------------------------------------------------

static volatile uint64_t s_frames;

void CtrPs4_NoteFrame(void)
{
	s_frames++;
}

static void *Ps4_Watchdog(void *arg)
{
	(void)arg;
	uint64_t lastFrames = 0, stillSince = Ps4_Seconds100(), lastReport = Ps4_Seconds100();
	uint64_t reportFrames = 0; // frames at the last status line (lastFrames moves every second)
	int reported = 0;
	for (;;)
	{
		sleep(1);
		const uint64_t now = Ps4_Seconds100();
		const uint64_t frames = s_frames;
		if (now - lastReport >= 1000)
		{
			CtrPs4_Log("status: %.1f fps", (double)(frames - reportFrames) * 100.0 / (double)(now - lastReport));
			lastReport = now;
			reportFrames = frames;
		}
		if (frames != lastFrames)
		{
			lastFrames = frames;
			stillSince = now;
			reported = 0;
		}
		else if (!reported && frames != 0 && now - stillSince > 2000)
		{
			CtrPs4_Log("!! no new frame for 20 s (after %lu frames)", (unsigned long)frames);
			reported = 1;
		}
	}
	return NULL;
}

// Runs from a constructor and, as a safety net, from the first SDL call the game makes.
// ⚠ Not constructor(101): the SDK's linker script keeps only the bare .init_array section, and a
// constructor with a priority goes to .init_array.00101, which is dropped - v0.1.x never ran this
// (no crash handler, no driver log, no fps lines in ps4.log).
void CtrPs4_EarlyInit(void)
{
	static int done;
	if (done)
	{
		return;
	}
	done = 1;
	CtrPs4_Log("CTR Turbocharged PS4 build %s", CTR_PS4_BUILD_ID);
	sceKernelInstallExceptionHandler(11 /* SIGSEGV */, Ps4_CrashHandler);
	sceKernelInstallExceptionHandler(10 /* SIGBUS */, Ps4_CrashHandler);
	sceKernelInstallExceptionHandler(4 /* SIGILL */, Ps4_CrashHandler);
	sceKernelInstallExceptionHandler(8 /* SIGFPE */, Ps4_CrashHandler);
	orbis_set_log(Ps4_OrbisLog);
	orbis_set_log_fatal(Ps4_OrbisLog);
	// The GPU driver's own log (read when EGL starts): why it refused a config, its budget lines.
	rename(CTR_PS4_DATA_DIR "mesa.log", CTR_PS4_DATA_DIR "mesa.old.log");
	setenv("MESA_LOG_FILE", CTR_PS4_DATA_DIR "mesa.log", 1);
	setenv("MESA_LOG_LEVEL", "info", 1);
	pthread_t watchdog;
	pthread_create(&watchdog, NULL, Ps4_Watchdog, NULL);
	pthread_detach(watchdog);
}

__attribute__((constructor)) static void Ps4_Constructor(void)
{
	CtrPs4_EarlyInit();
}

// ---- leaving --------------------------------------------------------------------------------
// Returning from main() on a retail console ends the process outside the system's expected path
// and shows the error dialog (CE-34878-0), whatever the teardown did (orbis-ports measured it in
// their RetroArch port). The way out is to ask the system: sceSystemServiceLoadExec("exit"). It is
// not expected to return; if it does, the code is logged and the old behaviour follows.

int32_t sceSystemServiceLoadExec(const char *path, const char *args[]);

void CtrPs4_Exit(void)
{
	CtrPs4_Log("shutdown: asking the system to close the application");
	const int32_t rc = sceSystemServiceLoadExec("exit", NULL);
	CtrPs4_Log("sceSystemServiceLoadExec(\"exit\") returned 0x%08x", (unsigned)rc);
}
