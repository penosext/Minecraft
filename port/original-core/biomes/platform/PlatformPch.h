#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// Both macros match upstream spelling: the header has PACAKGE, the cpp PACKAGE.
// They disable the optional game-debug override in the production pipeline.
#ifndef _CONTENT_PACKAGE
#define _CONTENT_PACKAGE
#endif
#ifndef _CONTENT_PACAKGE
#define _CONTENT_PACAKGE
#endif

namespace original_layers {
using std::shared_ptr;
using std::vector;
using std::wstring;
using std::string;
using byte = unsigned char;
using BYTE = unsigned char;
using DWORD = std::uint32_t;
using BOOL = int;
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(long) == 8 &&
              sizeof(std::int64_t) == 8, "Original layers require native LP64");
static_assert((-1 >> 1) == -1, "Original coordinate math needs arithmetic right shift");

// Native thread-local implementation of the three Win32 TLS primitives used by
// the unchanged IntCache.cpp. Storage ownership remains with original IntCache.
DWORD TlsAlloc();
void* TlsGetValue(DWORD index);
BOOL TlsSetValue(DWORD index, void* value);

// Only the optional, non-pipeline BiomeOverrideLayer uses these file APIs.
// They perform real host CPU file IO; no game-generation logic is stubbed.
using HANDLE = std::FILE*;
inline HANDLE const INVALID_HANDLE_VALUE = nullptr;
inline constexpr DWORD GENERIC_READ = 0x80000000u;
inline constexpr DWORD OPEN_EXISTING = 3;
inline constexpr DWORD FILE_ATTRIBUTE_NORMAL = 0x80;
inline constexpr BOOL FALSE = 0;
HANDLE CreateFile(const char*, DWORD, DWORD, void*, DWORD, DWORD, void*);
HANDLE CreateFile(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, void*);
DWORD GetLastError();
DWORD GetFileSize(HANDLE, void*);
BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
BOOL CloseHandle(HANDLE);
struct DebugApp {
    template<class... Args> void DebugPrintf(const char* fmt, Args... args) {
        std::fprintf(stderr, fmt, args...);
    }
    [[noreturn]] void FatalLoadError() { throw std::runtime_error("biomemap read failed"); }
};
inline DebugApp app;
}
#ifndef DEBUG_BREAK
#define DEBUG_BREAK() throw std::runtime_error("original biome override size error")
#endif

#include "OriginalHeaders.h"

namespace original_layers {
// Native implementation of the only System primitive required by layers.
class System {
public:
    static void arraycopy(intArray src, unsigned int srcPos, intArray* dst,
                          unsigned int dstPos, unsigned int length) {
        if (srcPos > src.length || length > src.length - srcPos ||
            dstPos > dst->length || length > dst->length - dstPos)
            throw std::out_of_range("original layer arraycopy");
        std::memmove(dst->data + dstPos, src.data + srcPos, sizeof(int) * length);
    }
};
}
