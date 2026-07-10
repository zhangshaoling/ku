# Kernel Benchmarks

Initial baseline captured on 2026-07-10.

## Environment

- CPU: 12th Gen Intel Core i5-12450H, 8 cores / 12 logical processors
- OS: Windows 11 Pro 64-bit, version 10.0.26200
- Compiler: MSYS2 UCRT64 g++ 16.1.0
- Flags: `-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -static`
- Runtime boundary: dynamically loaded `dao_kernel.dll` through the C ABI import library

## Baseline

Command:

```powershell
.\tools\benchmark_kernel.ps1 -SkipBuild
```

One run with 1,000,000 call iterations and 10,000 load iterations:

```text
module_bytes=4256
load_ns=13890.39
call_ns=103.03
calls_per_second=9705760.17
typed_ops_per_second=186964485.51
```

`call_ns` measures an exported two-instruction add function through the DLL/C ABI. `typed_ops_per_second` executes 256 `ADD_I64` instructions per call. These are microbenchmarks, not application throughput claims.

A second run after the same clean rebuild reported:

```text
load_ns=9789.94
call_ns=67.24
calls_per_second=14871281.62
typed_ops_per_second=285446042.89
```

The initial observed range is therefore 9.79-13.89 us per module load, 67-103 ns per C ABI call, and 187-285 M typed operations/s. A later benchmark phase must add repeated samples, warm/cold separation, percentiles, and allocation counters.

## Managed Clang Baseline

The repository-local LLVM-MinGW Clang 22.1.8 Release build (`-O3 -DNDEBUG`) produced:

```text
module_bytes=4256
load_ns=9129.26
call_ns=60.46
calls_per_second=16539724.28
typed_ops_per_second=281389492.52
```

This is a single local run with the same iteration counts, so it is a toolchain sanity check rather than a statistically rigorous compiler comparison.

## VM ABI 2 Host FFI Baseline

VM ABI 2 adds the required empty `IMPORT` section and benchmarks a direct numeric host callback. A Release run with 5,000,000 call iterations and 20,000 load iterations reported:

```text
module_bytes=4272
load_ns=10541.31
call_ns=83.54
host_call_ns=83.75
calls_per_second=11969986.93
typed_ops_per_second=209518730.57
```

`host_call_ns` includes `dao_vm_call`, one `CALL_HOST`, numeric registry lookup, the C callback, result validation, and `RETURN`. No JSON, string lookup, or allocation occurs per host call.

Future changes must report iteration count, hardware, variance, allocation count, binary size, and before/after values.
