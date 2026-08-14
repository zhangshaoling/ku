# Dao C++ Toolchain

The project uses a repository-local portable toolchain under `.toolchain/`. Nothing is added to the system PATH and no administrator install is required.

## Pinned Packages

| Package | Version | Purpose |
| --- | --- | --- |
| LLVM-MinGW UCRT x64 | 20260616 | Clang, LLD, compiler-rt, MinGW UCRT, sanitizer runtime |
| CMake | 4.3.3 | Cross-platform configuration and test generation |
| Ninja | 1.13.2 | Fast incremental builds |

Downloads come from the projects' GitHub releases and are verified with pinned SHA-256 hashes in [`tools/bootstrap_toolchain.ps1`](../tools/bootstrap_toolchain.ps1).

## Install Once

```powershell
.\tools\bootstrap_toolchain.ps1
```

Installed layout:

```text
.toolchain/
  cache/
  llvm-mingw/
  cmake/
  ninja/
  manifest.json
```

Verify the cached archives, installed commands, and compiler runtimes at any time:

```powershell
.\tools\doctor_toolchain.ps1
```

The doctor covers compilation, linking, CMake/Ninja builds, LLDB debugging, clangd, formatting, static analysis, dependency scanning, coverage, profiling, binary inspection, Windows resources, ASan, UBSan, and libFuzzer.

## Activate In The Current Shell

```powershell
. .\tools\activate_toolchain.ps1
```

This sets `CC`, `CXX`, `CMAKE_GENERATOR`, and prepends only the local tool directories to the current process PATH.

## Configure And Test

The normal entry point activates the managed tools, configures CMake, builds, and runs the tests:

```powershell
.\tools\build_kernel.ps1
```

The equivalent individual commands are:

```powershell
. .\tools\activate_toolchain.ps1
cmake -S . -B kernel/out/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build kernel/out/cmake
ctest --test-dir kernel/out/cmake --output-on-failure
```

Format and static-analysis checks:

```powershell
clang-format --dry-run --Werror `
  kernel/include/dao/dao.h kernel/include/dao/format.hpp `
  kernel/src/runtime.cpp kernel/src/module_builder.cpp
clang-tidy -p kernel/out/cmake kernel/src/runtime.cpp kernel/src/module_builder.cpp --quiet
```

Sanitizer build:

```powershell
cmake -S . -B kernel/out/sanitize -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DDAO_ENABLE_SANITIZERS=ON
cmake --build kernel/out/sanitize
ctest --test-dir kernel/out/sanitize --output-on-failure
```

The existing MSYS2 GCC remains a fallback for comparison builds, but it is not the managed project toolchain.
