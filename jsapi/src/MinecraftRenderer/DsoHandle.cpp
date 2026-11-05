// Fallback __dso_handle for toolchains whose link leaves it undefined.
//
// Every thread_local with a destructor registers through
// __cxa_thread_atexit(dtor, obj, &__dso_handle). The cross builds link with
// -unresolved-symbols=ignore-all, so a missing crtbeginS definition silently
// became address 0. glibc then took the null DSO path and dereferenced a null
// link_map on the first thread_local of a fresh thread: creating a world
// crashed the whole miniapp in the WorldJob thread (biome Source cache).
//
// Weak, so a toolchain crtbeginS.o strong definition still wins.
extern "C" {
__attribute__((weak, visibility("hidden"))) void* __dso_handle = &__dso_handle;
}
