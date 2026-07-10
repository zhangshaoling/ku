cmake_minimum_required(VERSION 3.25)
project(dao_kernel VERSION 0.1.0 LANGUAGES C CXX)

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
target_include_directories(dao_kernel PUBLIC kernel/include)
target_compile_definitions(dao_kernel PRIVATE DAO_KERNEL_SHARED DAO_KERNEL_BUILD)
target_compile_definitions(dao_kernel INTERFACE DAO_KERNEL_SHARED)
dao_enable_strict_warnings(dao_kernel)
dao_enable_sanitizers(dao_kernel)

add_library(dao_module_builder STATIC kernel/src/module_builder.cpp)
target_include_directories(dao_module_builder PUBLIC kernel/include)
dao_enable_strict_warnings(dao_module_builder)
dao_enable_sanitizers(dao_module_builder)


add_library(dao_disassembly STATIC kernel/src/disassemble.cpp)
target_include_directories(dao_disassembly PUBLIC kernel/include)
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
target_link_libraries(disassemble_tests PRIVATE dao_kernel dao_module_builder dao_disassembly dao_module_builder)
dao_enable_strict_warnings(disassemble_tests)
dao_enable_sanitizers(disassemble_tests)

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




