// SPDX-License-Identifier: GPL-3.0-or-later
// PS4 platform services shared by ps4/*.c: the diagnostics log (/data/ctr/ps4.log), crash reports
// and frame accounting.
#ifndef CTR_PS4_PLATFORM_H
#define CTR_PS4_PLATFORM_H

#include <stdarg.h>

#define CTR_PS4_DATA_DIR "/data/ctr/"

void CtrPs4_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void CtrPs4_LogV(const char *fmt, va_list args);

// Called once per presented frame (SDL_GL_SwapWindow): feeds the fps line and the hang watchdog.
void CtrPs4_NoteFrame(void);

#endif
