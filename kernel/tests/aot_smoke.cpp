#include <cstdint>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using Function = int (*)(const int64_t*, size_t, int64_t*);

#ifdef _WIN32
Function resolve(HMODULE library, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(library, name));
}
#else
Function resolve(void* library, const char* name) {
    return reinterpret_cast<Function>(dlsym(library, name));
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]);
    Function function = library == nullptr ? nullptr : resolve(library, "dao_aot_fn_0");
#else
    void* library = dlopen(argv[1], RTLD_NOW);
    Function function = library == nullptr ? nullptr : resolve(library, "dao_aot_fn_0");
#endif
    if (function == nullptr) return EXIT_FAILURE;
    const int64_t args[] = {40, 2}; int64_t result = 0;
    const bool ok = function(args, 2, &result) == 0 && result == 42;
    Function loop = resolve(library, "dao_aot_fn_1");
    const int64_t loop_args[] = {10};
    const bool loop_ok = loop != nullptr && loop(loop_args, 1, &result) == 0 && result == 55;
    Function raises = resolve(library, "dao_aot_fn_3");
    Function catches_local = resolve(library, "dao_aot_fn_4");
    Function catches_call = resolve(library, "dao_aot_fn_5");
    Function catches_nested = resolve(library, "dao_aot_fn_6");
    const bool exception_ok =
        raises != nullptr && raises(nullptr, 0, &result) == 9 &&
        catches_local != nullptr && catches_local(nullptr, 0, &result) == 0 && result == 42 &&
        catches_call != nullptr && catches_call(nullptr, 0, &result) == 0 && result == 42 &&
        catches_nested != nullptr && catches_nested(nullptr, 0, &result) == 0 && result == 42;
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    if (!ok || !loop_ok || !exception_ok) return EXIT_FAILURE;
    std::cout << "dao AOT smoke passed\n"; return EXIT_SUCCESS;
}
