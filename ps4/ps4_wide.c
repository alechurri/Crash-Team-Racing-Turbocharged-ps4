// SPDX-License-Identifier: GPL-3.0-or-later
// 32-bit wide-character memory functions for the PS4 executable.
//
// The OpenOrbis SDK's static wmemchr (and the other wmem*/wcs* functions) work on 16-bit units,
// while clang's wchar_t on this target is 32-bit. libc++ turns std::find over any 4-byte trivially
// comparable type into __builtin_wmemchr, so C++ code linked into this program (Mesa's shader
// compilers among it) would get searches that only find the first element. Found while porting
// the Eden emulator to the PS4, where it corrupted a cache for weeks. Defining them here, in the
// executable, makes every caller use these.
//
// Built with -fno-builtin so the loops are not turned back into calls to themselves.
//
// Written with int32_t on purpose, not wchar_t: in C the SDK's headers make wchar_t 16-bit, which
// is exactly the bug. The callers that matter are C++ (4-byte wchar_t) and only see the symbol.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int32_t wchar4;

wchar4 *wmemchr(const wchar4 *s, wchar4 c, size_t n)
{
	for (; n != 0; --n, ++s)
	{
		if (*s == c)
		{
			return (wchar4 *)s;
		}
	}
	return NULL;
}

int wmemcmp(const wchar4 *a, const wchar4 *b, size_t n)
{
	for (; n != 0; --n, ++a, ++b)
	{
		if (*a != *b)
		{
			return *a < *b ? -1 : 1;
		}
	}
	return 0;
}

wchar4 *wmemcpy(wchar4 *dst, const wchar4 *src, size_t n)
{
	memcpy(dst, src, n * sizeof(wchar4));
	return dst;
}

wchar4 *wmemmove(wchar4 *dst, const wchar4 *src, size_t n)
{
	memmove(dst, src, n * sizeof(wchar4));
	return dst;
}

wchar4 *wmemset(wchar4 *dst, wchar4 c, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		dst[i] = c;
	}
	return dst;
}

size_t wcslen(const wchar4 *s)
{
	const wchar4 *p = s;
	while (*p != 0)
	{
		++p;
	}
	return (size_t)(p - s);
}

int wcscmp(const wchar4 *a, const wchar4 *b)
{
	for (;; ++a, ++b)
	{
		if (*a != *b)
		{
			return *a < *b ? -1 : 1;
		}
		if (*a == 0)
		{
			return 0;
		}
	}
}

int wcsncmp(const wchar4 *a, const wchar4 *b, size_t n)
{
	for (; n != 0; --n, ++a, ++b)
	{
		if (*a != *b)
		{
			return *a < *b ? -1 : 1;
		}
		if (*a == 0)
		{
			return 0;
		}
	}
	return 0;
}

wchar4 *wcschr(const wchar4 *s, wchar4 c)
{
	for (;; ++s)
	{
		if (*s == c)
		{
			return (wchar4 *)s;
		}
		if (*s == 0)
		{
			return NULL;
		}
	}
}
