#include "platform/native_log.h"

#include <macros.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "platform/native_win32.h"
#endif

global_variable FILE *s_logStream = NULL;
global_variable char s_logPath[512]; // TODO(aalhendi): yeah this is an issue waiting to happen. w/e

internal void Platform_LogWrite(FILE *consoleStream, const char *text)
{
	FILE *stream = (consoleStream != NULL) ? consoleStream : stdout;

#ifdef _WIN32
	OutputDebugStringA(text);
#endif

	fputs(text, stream);

	if (s_logStream != NULL)
	{
		fputs(text, s_logStream);
		fflush(s_logStream);
	}
}

internal void Platform_LogV(FILE *consoleStream, const char *fmt, va_list args)
{
	char text[4096];
	int written = vsnprintf(text, sizeof(text), fmt, args);

	if (written < 0)
	{
		return;
	}

	text[sizeof(text) - 1] = '\0';
	Platform_LogWrite(consoleStream, text);
}

int Platform_LogSetPath(const char *path)
{
	int written;

	if (s_logStream != NULL)
	{
		return 0;
	}

	if ((path == NULL) || (path[0] == '\0'))
	{
		s_logPath[0] = '\0';
		return 1;
	}

	written = snprintf(s_logPath, sizeof(s_logPath), "%s", path);
	if ((written < 0) || ((size_t)written >= sizeof(s_logPath)))
	{
		s_logPath[0] = '\0';
		fprintf(stderr, "[CTR Native] Error: log path is too long\n");
		return 0;
	}

	return 1;
}

const char *Platform_LogGetPath(void)
{
	return s_logPath;
}

#if defined(_WIN32) && defined(_M_X64) || defined(_WIN32) && defined(__x86_64__)
// Diagnostic build: log any crash with its code, address and the frame-pointer chain, as
// addresses to symbolize against the executable.
internal LONG WINAPI Platform_LogCrashFilter(EXCEPTION_POINTERS *info)
{
	const CONTEXT *ctx = info->ContextRecord;
	char text[256];
	snprintf(text, sizeof(text), "CRASH: exception 0x%08lx at %p (rsp %p), module base %p\n",
	         (unsigned long)info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress,
	         (void *)ctx->Rsp, (void *)GetModuleHandleA(NULL));
	Platform_LogWrite(stderr, text);
	if (info->ExceptionRecord->NumberParameters >= 2)
	{
		snprintf(text, sizeof(text), "CRASH:   access %s %p\n",
		         info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
		         (void *)info->ExceptionRecord->ExceptionInformation[1]);
		Platform_LogWrite(stderr, text);
	}
	void **frame = (void **)ctx->Rbp;
	for (int depth = 0; depth < 16 && frame != NULL; ++depth)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery(frame, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
		{
			break;
		}
		snprintf(text, sizeof(text), "CRASH:   #%d return %p\n", depth, frame[1]);
		Platform_LogWrite(stderr, text);
		void **next = (void **)frame[0];
		if (next <= frame)
		{
			break;
		}
		frame = next;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void Platform_LogInit(const char *appName)
{
	if (s_logPath[0] == '\0')
	{
		int written = snprintf(s_logPath, sizeof(s_logPath), "%s.log", appName);

		if ((written < 0) || ((size_t)written >= sizeof(s_logPath)))
		{
			fprintf(stderr, "[CTR Native] Error: log filename is too long\n");
			s_logPath[0] = '\0';
			return;
		}

		// A ':' in the title ("Crash Team Racing: Turbocharged") makes Windows write the log into
		// an alternate data stream of a file named "Crash Team Racing", where nobody finds it.
		for (char *c = s_logPath; *c != '\0'; ++c)
		{
			if (*c == ':')
			{
				*c = '-';
			}
		}
	}

	s_logStream = fopen(s_logPath, "wb");

	if (s_logStream == NULL)
	{
		fprintf(stderr, "[CTR Native] Error: cannot create log file '%s'\n", s_logPath);
	}
#if defined(_WIN32) && defined(_M_X64) || defined(_WIN32) && defined(__x86_64__)
	SetUnhandledExceptionFilter(Platform_LogCrashFilter);
#endif
}

void Platform_LogShutdown(void)
{
	Platform_LogWarn("---- LOG CLOSED ----\n");

	if (s_logStream != NULL)
	{
		fclose(s_logStream);
	}

	s_logStream = NULL;
}

void Platform_LogFlush(void)
{
	if (s_logStream != NULL)
	{
		fflush(s_logStream);
	}
}

void Platform_Log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stdout, fmt, args);
	va_end(args);
}

void Platform_LogWarn(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stdout, fmt, args);
	va_end(args);
}

void Platform_LogError(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stderr, fmt, args);
	va_end(args);
}
