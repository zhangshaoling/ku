#include <cstdint>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]);
    auto function = reinterpret_cast<int (*)(const int64_t*, size_t, int64_t*)>(
        library == nullptr ? nullptr : GetProcAddress(library, "dao_aot_fn_0"));
#else
    void* library = dlopen(argv[1], RTLD_NOW);
    auto function = reinterpret_cast<int (*)(const int64_t*, size_t, int64_t*)>(
        library == nullptr ? nullptr : dlsym(library, "dao_aot_fn_0"));
#endif
    if (function == nullptr) return EXIT_FAILURE;
    const int64_t args[] = {40, 2}; int64_t result = 0;
    const bool ok = function(args, 2, &result) == 0 && result == 42;
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    if (!ok) return EXIT_FAILURE;
    std::cout << "dao AOT smoke passed\n"; return EXIT_SUCCESS;
}
