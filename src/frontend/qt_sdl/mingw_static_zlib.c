// MinGW static builds only: MSYS2's static libarchive.a was compiled against the zlib DLL and
// calls zlib through import pointers (__imp_inflate, ...). Provide those pointers for the
// static zlib, so the executable needs no zlib1.dll.
#include <zlib.h>

#define ZIMP(f) void* __imp_##f = (void*)&f;
ZIMP(crc32)
ZIMP(inflate)
ZIMP(inflateEnd)
ZIMP(inflateInit_)
ZIMP(inflateInit2_)
ZIMP(inflateReset)
ZIMP(inflateSetDictionary)
