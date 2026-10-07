#include <ctr_ptr32.h>

#if defined(CTR_NATIVE_64BIT)
#include <stdio.h>
#include <stdlib.h>

// Holds the origin for CtrPtr32 handles (see ctr_ptr32.h). Nothing else may
// ever be allocated inside it.
CtrPtr32Anchor gCtrPtr32Anchor;

void CtrPtr32_RangeError(uintptr_t p)
{
	fprintf(stderr, "CtrPtr32: pointer %p is outside +-2 GiB of the image\n", (void *)p);
	// Also to a file (stderr is invisible in a GUI build), with the return addresses that led
	// here (frame-pointer walk) so the caller can be symbolized.
	FILE *out = fopen("ctr-ptr32-error.txt", "w");
	if (out == NULL)
	{
		out = stderr;
	}
	fprintf(out, "CtrPtr32: pointer %p is outside +-2 GiB of the image (anchor %p)\n", (void *)p,
	        (void *)&gCtrPtr32Anchor);
	void **frame = (void **)__builtin_frame_address(0);
	for (int depth = 0; depth < 16 && frame != NULL; ++depth)
	{
		void *ret = frame[1];
		if (ret == NULL)
		{
			break;
		}
		fprintf(out, "CtrPtr32:   #%d return %p\n", depth, ret);
		void **next = (void **)frame[0];
		if (next <= frame)
		{
			break;
		}
		frame = next;
	}
	fflush(out);
	abort();
}
#endif
