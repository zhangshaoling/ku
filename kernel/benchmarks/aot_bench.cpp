#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using Function = int (*)(const int64_t*, size_t, int64_t*);
double run(Function function, uint64_t iterations) {
    const int64_t args[] = {40, 2}; int64_t result = 0; volatile int64_t sink = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) { if (function(args, 2, &result) != 0) return 0; sink = result; }
    const auto end = std::chrono::steady_clock::now();
    if (sink != 42) return 0;
    return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(iterations);
}

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]); Function aot = reinterpret_cast<Function>(library ? GetProcAddress(library, "dao_aot_fn_0") : nullptr); Function native = reinterpret_cast<Function>(library ? GetProcAddress(library, "dao_aot_native_add_baseline") : nullptr);
#else
    void* library = dlopen(argv[1], RTLD_NOW); Function aot = reinterpret_cast<Function>(library ? dlsym(library, "dao_aot_fn_0") : nullptr); Function native = reinterpret_cast<Function>(library ? dlsym(library, "dao_aot_native_add_baseline") : nullptr);
#endif
    if (!aot || !native) return EXIT_FAILURE;
    constexpr uint64_t iterations = 2'000'000; const double native_ns = run(native, iterations); const double aot_ns = run(aot, iterations);
    const double ratio = native_ns / aot_ns;
    std::cout << std::fixed << std::setprecision(2) << "native_ns=" << native_ns << "\naot_ns=" << aot_ns << "\naot_native_ratio=" << ratio << '\n';
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return native_ns > 0 && aot_ns > 0 && ratio >= 0.8 ? EXIT_SUCCESS : EXIT_FAILURE;
}
