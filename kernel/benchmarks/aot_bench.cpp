#include <chrono>
#include <algorithm>
#include <array>
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
double run(Function function, const int64_t* args, size_t count, int64_t expected, uint64_t iterations) {
    int64_t result = 0; volatile int64_t sink = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) { if (function(args, count, &result) != 0) return 0; sink = result; }
    const auto end = std::chrono::steady_clock::now();
    if (sink != expected) return 0;
    return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(iterations);
}

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]); Function aot = reinterpret_cast<Function>(library ? GetProcAddress(library, "dao_aot_fn_0") : nullptr); Function native = reinterpret_cast<Function>(library ? GetProcAddress(library, "dao_aot_native_add_baseline") : nullptr);
#else
    void* library = dlopen(argv[1], RTLD_NOW); Function aot = reinterpret_cast<Function>(library ? dlsym(library, "dao_aot_fn_0") : nullptr); Function native = reinterpret_cast<Function>(library ? dlsym(library, "dao_aot_native_add_baseline") : nullptr);
#endif
    Function aot_loop = reinterpret_cast<Function>(
#ifdef _WIN32
        GetProcAddress(library, "dao_aot_fn_2")
#else
        dlsym(library, "dao_aot_fn_2")
#endif
    );
    Function native_loop = reinterpret_cast<Function>(
#ifdef _WIN32
        GetProcAddress(library, "dao_aot_native_sum_baseline")
#else
        dlsym(library, "dao_aot_native_sum_baseline")
#endif
    );
    if (!aot || !native || !aot_loop || !native_loop) return EXIT_FAILURE;
    const int64_t add_args[] = {40, 2};
    constexpr uint64_t iterations = 5'000'000; std::array<double, 7> native_samples{}; std::array<double, 7> aot_samples{};
    for (size_t i = 0; i < native_samples.size(); ++i) {
        if ((i & 1u) == 0) { native_samples[i] = run(native, add_args, 2, 42, iterations); aot_samples[i] = run(aot, add_args, 2, 42, iterations); }
        else { aot_samples[i] = run(aot, add_args, 2, 42, iterations); native_samples[i] = run(native, add_args, 2, 42, iterations); }
    }
    std::sort(native_samples.begin(), native_samples.end()); std::sort(aot_samples.begin(), aot_samples.end());
    const double native_ns = native_samples[native_samples.size() / 2]; const double aot_ns = aot_samples[aot_samples.size() / 2];
    const double ratio = native_ns / aot_ns;
    const int64_t loop_args[] = {100};
    const double native_loop_ns = run(native_loop, loop_args, 1, 5050, 500'000);
    const double aot_loop_ns = run(aot_loop, loop_args, 1, 5050, 500'000);
    const double loop_ratio = native_loop_ns / aot_loop_ns;
    std::cout << std::fixed << std::setprecision(2) << "native_ns=" << native_ns << "\naot_ns=" << aot_ns << "\naot_native_ratio=" << ratio << "\nnative_loop_ns=" << native_loop_ns << "\naot_loop_ns=" << aot_loop_ns << "\naot_loop_native_ratio=" << loop_ratio << '\n';
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return native_ns > 0 && aot_ns > 0 && ratio >= 0.8 && loop_ratio >= 0.8 ? EXIT_SUCCESS : EXIT_FAILURE;
}
