cmake_minimum_required(VERSION 3.25)
project(dao_kernel VERSION 0.2.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Python3 COMPONENTS Interpreter QUIET)
get_filename_component(DAO_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

option(DAO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DAO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(DAO_BUILD_FUZZER "Build the libFuzzer module loader target" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

function(dao_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(DAO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(DAO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(dao_enable_sanitizers target)
    if(DAO_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(dao_kernel SHARED kernel/src/runtime.cpp)
target_include_directories(dao_kernel PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(dao_kernel PRIVATE DAO_KERNEL_SHARED DAO_KERNEL_BUILD)
target_compile_definitions(dao_kernel INTERFACE DAO_KERNEL_SHARED)
dao_enable_strict_warnings(dao_kernel)
dao_enable_sanitizers(dao_kernel)

add_library(dao_module_builder STATIC kernel/src/module_builder.cpp)
target_include_directories(dao_module_builder PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include> $<INSTALL_INTERFACE:include>)
dao_enable_strict_warnings(dao_module_builder)
dao_enable_sanitizers(dao_module_builder)


add_library(dao_disassembly STATIC kernel/src/disassemble.cpp)
target_include_directories(dao_disassembly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include> $<INSTALL_INTERFACE:include>)
dao_enable_strict_warnings(dao_disassembly)
dao_enable_sanitizers(dao_disassembly)

add_executable(kernel_tests kernel/tests/kernel_tests.cpp)
target_link_libraries(kernel_tests PRIVATE dao_kernel dao_module_builder)
dao_enable_strict_warnings(kernel_tests)
dao_enable_sanitizers(kernel_tests)

add_executable(abi_smoke kernel/tests/abi_smoke.cpp)
target_link_libraries(abi_smoke PRIVATE dao_kernel dao_module_builder)
dao_enable_strict_warnings(abi_smoke)
dao_enable_sanitizers(abi_smoke)

add_executable(c_header_smoke kernel/tests/c_header_smoke.c)
target_link_libraries(c_header_smoke PRIVATE dao_kernel)
dao_enable_strict_warnings(c_header_smoke)
dao_enable_sanitizers(c_header_smoke)

add_executable(kernel_bench kernel/benchmarks/kernel_bench.cpp)
target_link_libraries(kernel_bench PRIVATE dao_kernel dao_module_builder)
dao_enable_strict_warnings(kernel_bench)
dao_enable_sanitizers(kernel_bench)

add_executable(disassemble_tests kernel/tests/disassemble_tests.cpp)
target_link_libraries(disassemble_tests PRIVATE dao_kernel dao_module_builder dao_disassembly)
dao_enable_strict_warnings(disassemble_tests)
dao_enable_sanitizers(disassemble_tests)

add_library(dao_assembly STATIC kernel/src/assemble.cpp)
target_include_directories(dao_assembly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(dao_assembly PUBLIC dao_module_builder)
dao_enable_strict_warnings(dao_assembly)
dao_enable_sanitizers(dao_assembly)

add_executable(assemble_tests kernel/tests/assemble_tests.cpp)
target_link_libraries(assemble_tests PRIVATE dao_kernel dao_module_builder dao_disassembly dao_assembly)
dao_enable_strict_warnings(assemble_tests)
dao_enable_sanitizers(assemble_tests)

add_library(dao_ku_migration STATIC kernel/src/ku_migration.cpp)
target_include_directories(dao_ku_migration PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(dao_ku_migration PUBLIC dao_module_builder dao_disassembly)
dao_enable_strict_warnings(dao_ku_migration)
dao_enable_sanitizers(dao_ku_migration)

add_executable(ku_migration_tests kernel/tests/ku_migration_tests.cpp)
target_link_libraries(ku_migration_tests PRIVATE dao_kernel dao_module_builder dao_disassembly dao_assembly dao_ku_migration)
dao_enable_strict_warnings(ku_migration_tests)
dao_enable_sanitizers(ku_migration_tests)

add_executable(ku_std_math_tests kernel/tests/ku_std_math_tests.cpp)
target_link_libraries(ku_std_math_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(ku_std_math_tests)
dao_enable_sanitizers(ku_std_math_tests)

add_executable(ku_std_list_tests kernel/tests/ku_std_list_tests.cpp)
target_link_libraries(ku_std_list_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(ku_std_list_tests)
dao_enable_sanitizers(ku_std_list_tests)

add_executable(ku_std_fs_tests kernel/tests/ku_std_fs_tests.cpp)
target_link_libraries(ku_std_fs_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(ku_std_fs_tests)
dao_enable_sanitizers(ku_std_fs_tests)

add_executable(ku_std_debug_tests kernel/tests/ku_std_debug_tests.cpp)
target_link_libraries(ku_std_debug_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(ku_std_debug_tests)
dao_enable_sanitizers(ku_std_debug_tests)

add_executable(ku_std_string_tests kernel/tests/ku_std_string_tests.cpp)
target_link_libraries(ku_std_string_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(ku_std_string_tests)
dao_enable_sanitizers(ku_std_string_tests)

add_executable(selfhost_tests kernel/tests/selfhost_tests.cpp)
target_link_libraries(selfhost_tests PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(selfhost_tests)
dao_enable_sanitizers(selfhost_tests)

add_executable(dao-ku kernel/tools/ku_compile.cpp)
target_link_libraries(dao-ku PRIVATE dao_kernel dao_module_builder dao_ku_migration)
dao_enable_strict_warnings(dao-ku)
dao_enable_sanitizers(dao-ku)

add_executable(dao-aot kernel/tools/aot.cpp)
target_link_libraries(dao-aot PRIVATE dao_disassembly)
dao_enable_strict_warnings(dao-aot)
dao_enable_sanitizers(dao-aot)

add_executable(aot_smoke kernel/tests/aot_smoke.cpp)
dao_enable_strict_warnings(aot_smoke)
dao_enable_sanitizers(aot_smoke)
if(CMAKE_DL_LIBS)
    target_link_libraries(aot_smoke PRIVATE ${CMAKE_DL_LIBS})
endif()

add_executable(aot_bench kernel/benchmarks/aot_bench.cpp)
dao_enable_strict_warnings(aot_bench)
dao_enable_sanitizers(aot_bench)
if(CMAKE_DL_LIBS)
    target_link_libraries(aot_bench PRIVATE ${CMAKE_DL_LIBS})
endif()

if(DAO_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DAO_BUILD_FUZZER requires Clang and libFuzzer")
    endif()

    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
        OUTPUT_VARIABLE DAO_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(DAO_FUZZER_RUNTIME
        "${DAO_CLANG_RESOURCE_DIR}/lib/windows/libclang_rt.fuzzer-x86_64.a")
    if(NOT EXISTS "${DAO_FUZZER_RUNTIME}")
        message(FATAL_ERROR "Clang libFuzzer runtime not found: ${DAO_FUZZER_RUNTIME}")
    endif()

    add_executable(dao_module_fuzz
        kernel/fuzz/module_fuzz.cpp
        kernel/src/runtime.cpp
    )
    target_include_directories(dao_module_fuzz PRIVATE kernel/include)
    target_compile_options(dao_module_fuzz PRIVATE
        -fsanitize-coverage=inline-8bit-counters,indirect-calls,trace-cmp,pc-table
    )
    target_link_libraries(dao_module_fuzz PRIVATE "${DAO_FUZZER_RUNTIME}")
    dao_enable_strict_warnings(dao_module_fuzz)
    dao_enable_sanitizers(dao_module_fuzz)

    add_executable(dao_fuzz_seed kernel/fuzz/seed_generator.cpp)
    target_link_libraries(dao_fuzz_seed PRIVATE dao_module_builder)
    dao_enable_strict_warnings(dao_fuzz_seed)
    dao_enable_sanitizers(dao_fuzz_seed)
endif()

enable_testing()
add_test(NAME kernel_conformance COMMAND kernel_tests)
add_test(NAME dynamic_cpp_abi COMMAND abi_smoke)
add_test(NAME pure_c_abi COMMAND c_header_smoke)
add_test(NAME disassemble COMMAND disassemble_tests)
add_test(NAME assemble COMMAND assemble_tests)
add_test(NAME ku_migration COMMAND ku_migration_tests)
add_test(NAME ku_selfhost_seed COMMAND selfhost_tests "${CMAKE_SOURCE_DIR}/kernel/selfhost/compiler.ku")
add_test(NAME ku_compile_stdlib COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/core.ku"
    "${CMAKE_BINARY_DIR}/stdlib_core.dao")
add_test(NAME ku_compile_std_math COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/math.ku"
    "${CMAKE_BINARY_DIR}/stdlib_math.dao")
add_test(NAME ku_std_math COMMAND ku_std_math_tests
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/math.ku")
add_test(NAME ku_compile_std_list COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/list.ku"
    "${CMAKE_BINARY_DIR}/stdlib_list.dao")
add_test(NAME ku_std_list COMMAND ku_std_list_tests
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/list.ku")
add_test(NAME ku_compile_std_fs COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/fs.ku"
    "${CMAKE_BINARY_DIR}/stdlib_fs.dao")
add_test(NAME ku_std_fs COMMAND ku_std_fs_tests
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/fs.ku"
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/string.ku"
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/io.ku")
add_test(NAME ku_compile_std_debug COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/debug.ku"
    "${CMAKE_BINARY_DIR}/stdlib_debug.dao")
add_test(NAME ku_std_debug COMMAND ku_std_debug_tests
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/debug.ku")
add_test(NAME ku_compile_std_string COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/string.ku"
    "${CMAKE_BINARY_DIR}/stdlib_string.dao")
add_test(NAME ku_std_string COMMAND ku_std_string_tests
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/string.ku")
add_test(NAME ku_compile_std_io COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/stdlib/io.ku"
    "${CMAKE_BINARY_DIR}/stdlib_io.dao")
add_test(NAME ku_compile_acceptance COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/tests/fixtures/language_acceptance.ku"
    "${CMAKE_BINARY_DIR}/language_acceptance.dao")
add_test(NAME ku_compile_legacy_import COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/tests/fixtures/legacy_import_main.ku"
    "${CMAKE_BINARY_DIR}/legacy_import.dao")
add_test(NAME ku_reject_import_escape COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/tests/fixtures/legacy_import_escape.ku"
    "${CMAKE_BINARY_DIR}/legacy_import_escape.dao")
set_tests_properties(ku_reject_import_escape PROPERTIES WILL_FAIL TRUE)
add_test(NAME aot_compile_module COMMAND dao-ku
    "${CMAKE_SOURCE_DIR}/kernel/tests/fixtures/aot_numeric.ku"
    "${CMAKE_BINARY_DIR}/aot_numeric.dao")
add_test(NAME aot_generate_c COMMAND dao-aot
    "${CMAKE_BINARY_DIR}/aot_numeric.dao"
    "${CMAKE_BINARY_DIR}/aot_numeric.c")
set_tests_properties(aot_generate_c PROPERTIES DEPENDS aot_compile_module)
add_test(NAME aot_compile_native COMMAND ${CMAKE_C_COMPILER}
    -std=c17 -O3 -shared "${CMAKE_BINARY_DIR}/aot_numeric.c"
    -o "${CMAKE_BINARY_DIR}/aot_numeric${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_tests_properties(aot_compile_native PROPERTIES DEPENDS aot_generate_c)
add_test(NAME aot_execute_native COMMAND aot_smoke
    "${CMAKE_BINARY_DIR}/aot_numeric${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_tests_properties(aot_execute_native PROPERTIES DEPENDS aot_compile_native)
add_test(NAME aot_performance_gate COMMAND aot_bench
    "${CMAKE_BINARY_DIR}/aot_numeric${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_tests_properties(aot_performance_gate PROPERTIES DEPENDS aot_compile_native)

# LLVM-MinGW links C++ targets against shared libc++/libunwind runtimes. Make
# CTest self-contained instead of requiring callers to activate the toolchain.
set(DAO_RUNTIME_TESTS
    kernel_conformance
    dynamic_cpp_abi
    pure_c_abi
    disassemble
    assemble
    ku_migration
    ku_selfhost_seed
    ku_compile_stdlib
    ku_compile_std_math
    ku_std_math
    ku_compile_std_list
    ku_std_list
    ku_compile_std_fs
    ku_std_fs
    ku_compile_std_debug
    ku_std_debug
    ku_compile_std_string
    ku_std_string
    ku_compile_std_io
    ku_compile_acceptance
    ku_compile_legacy_import
    ku_reject_import_escape
    aot_compile_module
    aot_generate_c
    aot_execute_native
    aot_performance_gate)
set_tests_properties(${DAO_RUNTIME_TESTS} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DAO_COMPILER_DIR}")

if(Python3_Interpreter_FOUND)
    add_test(NAME python_binding COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/bindings/python/test_binding.py"
        "$<TARGET_FILE:dao_kernel>"
        "${CMAKE_BINARY_DIR}/aot_numeric.dao")
    set_tests_properties(python_binding PROPERTIES DEPENDS aot_compile_module)
    set_tests_properties(python_binding PROPERTIES ENVIRONMENT "DAO_KERNEL_DLL_DIRS=${DAO_COMPILER_DIR}")
endif()
find_program(DAO_CARGO_EXECUTABLE cargo)
if(DAO_CARGO_EXECUTABLE)
    add_test(NAME rust_binding_check COMMAND ${DAO_CARGO_EXECUTABLE} check
        --manifest-path "${CMAKE_SOURCE_DIR}/bindings/rust/dao-kernel-sys/Cargo.toml")
endif()

install(TARGETS dao_kernel dao_module_builder dao_disassembly dao_assembly dao_ku_migration dao-ku dao-aot
    EXPORT DaoKernelTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY kernel/include/dao DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES kernel/FORMAT.md kernel/FFI.md kernel/README.md kernel/BENCHMARKS.md kernel/K5.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES kernel/selfhost/compiler.ku DESTINATION share/dao/selfhost)
install(FILES kernel/stdlib/core.ku kernel/stdlib/math.ku kernel/stdlib/list.ku DESTINATION share/dao/stdlib)
install(FILES docs/KU_SELFHOST.md docs/ABI_FREEZE_AUDIT.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY bindings/python/dao_kernel DESTINATION bindings/python
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
install(DIRECTORY bindings/rust/dao-kernel-sys DESTINATION bindings/rust
    PATTERN "target" EXCLUDE)
install(EXPORT DaoKernelTargets
    FILE DaoKernelTargets.cmake
    NAMESPACE Dao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DaoKernel)
configure_package_config_file(cmake/DaoKernelConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/DaoKernelConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DaoKernel)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DaoKernelConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DaoKernelConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DaoKernelConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DaoKernel)

set(CPACK_PACKAGE_NAME "DaoKernel")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)
