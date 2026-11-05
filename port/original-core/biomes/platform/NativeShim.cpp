#include "PlatformPch.h"

#include <cerrno>
#include <string>

namespace original_layers {
namespace {
std::atomic<DWORD> nextTls{0};
thread_local vector<void*> tlsValues;
}
DWORD TlsAlloc() { return nextTls.fetch_add(1, std::memory_order_relaxed); }
void* TlsGetValue(DWORD index) {
    return index < tlsValues.size() ? tlsValues[index] : nullptr;
}
BOOL TlsSetValue(DWORD index, void* value) {
    if (tlsValues.size() <= index) tlsValues.resize(static_cast<std::size_t>(index) + 1);
    tlsValues[index] = value;
    return 1;
}

HANDLE CreateFile(const char* path, DWORD, DWORD, void*, DWORD, DWORD, void*) {
    return std::fopen(path, "rb");
}
// wchar_t is UTF-32 on Linux; encode it as UTF-8, which is what
// std::filesystem::path(...).string() produced. Hand-written so the shim does
// not need <filesystem>, which the GCC 7 device cross toolchain lacks.
HANDLE CreateFile(const wchar_t* path, DWORD a, DWORD b, void* c, DWORD d, DWORD e, void* f) {
    std::string utf8;
    for (; *path; ++path) {
        const auto cp = static_cast<unsigned long>(*path);
        if (cp < 0x80) utf8 += char(cp);
        else if (cp < 0x800) { utf8 += char(0xC0 | (cp >> 6)); utf8 += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { utf8 += char(0xE0 | (cp >> 12)); utf8 += char(0x80 | ((cp >> 6) & 0x3F));
                                 utf8 += char(0x80 | (cp & 0x3F)); }
        else { utf8 += char(0xF0 | (cp >> 18)); utf8 += char(0x80 | ((cp >> 12) & 0x3F));
               utf8 += char(0x80 | ((cp >> 6) & 0x3F)); utf8 += char(0x80 | (cp & 0x3F)); }
    }
    return CreateFile(utf8.c_str(), a, b, c, d, e, f);
}
DWORD GetLastError() { return static_cast<DWORD>(errno); }
DWORD GetFileSize(HANDLE file, void*) {
    const auto pos = std::ftell(file);
    if (pos < 0 || std::fseek(file, 0, SEEK_END)) throw std::runtime_error("biomemap seek failed");
    const auto size = std::ftell(file);
    if (size < 0 || std::fseek(file, pos, SEEK_SET)) throw std::runtime_error("biomemap size failed");
    if (static_cast<unsigned long>(size) > std::numeric_limits<DWORD>::max())
        throw std::runtime_error("biomemap too large");
    return static_cast<DWORD>(size);
}
BOOL ReadFile(HANDLE file, void* out, DWORD size, DWORD* read, void*) {
    *read = static_cast<DWORD>(std::fread(out, 1, size, file));
    return std::ferror(file) ? 0 : 1;
}
BOOL CloseHandle(HANDLE file) { return std::fclose(file) == 0; }
} // namespace original_layers
