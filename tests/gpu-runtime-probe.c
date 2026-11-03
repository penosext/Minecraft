#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    const char* names[] = {"libEGL.so.1", "libEGL.so", "libGLESv2.so.2", "libGLESv2.so", "libmali.so"};
    int egl = 0, gles = 0;
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        void* library = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (!library) { printf("%s unavailable: %s\n", names[i], dlerror()); continue; }
        const int hasEgl = dlsym(library, "eglGetDisplay") != NULL && dlsym(library, "eglInitialize") != NULL;
        const int hasGles = dlsym(library, "glGetString") != NULL && dlsym(library, "glDrawArrays") != NULL;
        egl |= hasEgl; gles |= hasGles;
        printf("%s loaded EGL=%d GLES=%d\n", names[i], hasEgl, hasGles);
        dlclose(library);
    }
    printf("EGL runtime available=%d GLES runtime available=%d\n", egl, gles);
    puts("Read-only runtime availability probe; no graphics context or display takeover attempted.");
    return egl && gles ? 0 : 2;
}
