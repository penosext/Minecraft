// Fallback __dso_handle. CMakeLists.txt builds with -fuse-cxa-atexit, so
// thread_local destructors register through
// __cxa_thread_atexit(dtor, obj, &__dso_handle). The link uses
// -unresolved-symbols=ignore-all, so a toolchain whose crtbeginS.o lacks the
// symbol would silently make that address 0, and glibc would crash on the null
// DSO path. Weak, so the usual crtbeginS.o strong definition still wins.
extern "C" {
__attribute__((weak, visibility("hidden"))) void* __dso_handle = &__dso_handle;
}
