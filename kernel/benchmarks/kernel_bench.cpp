#include "dao/dao.h"
#include "dao/format.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ns(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::nano>(end - start).count();
}

dao_status host_add(void*, const dao_value* args, size_t arg_count, dao_value* out_value) {
    if (arg_count != 2 || args == nullptr || out_value == nullptr ||
        args[0].type != DAO_VALUE_I64 || args[1].type != DAO_VALUE_I64) {
        return DAO_INVALID_ARGUMENT;
    }
    *out_value = {DAO_VALUE_I64, 0, args[0].payload + args[1].payload};
    return DAO_OK;
}

} // namespace

int main(int argc, char** argv) {
    uint64_t call_iterations = 1'000'000;
    uint64_t load_iterations = 10'000;
    if (argc > 1)
        call_iterations = std::strtoull(argv[1], nullptr, 10);
    if (argc > 2)
        load_iterations = std::strtoull(argv[2], nullptr, 10);
    if (call_iterations == 0 || load_iterations == 0)
        return EXIT_FAILURE;

    dao::ModuleBuilder builder;
    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {
        {dao::Opcode::AddI64, 0, 2, 0, 1, 0},
        {dao::Opcode::Return, 0, 0, 2, 0, 0},
    };
    const uint32_t function_index = builder.add_function(std::move(add));
    builder.add_export(1, function_index);

    constexpr uint32_t arithmetic_ops_per_call = 256;
    dao::FunctionSpec arithmetic;
    arithmetic.parameter_count = 2;
    arithmetic.register_count = 3;
    arithmetic.code.reserve(arithmetic_ops_per_call + 1);
    for (uint32_t index = 0; index < arithmetic_ops_per_call; ++index) {
        arithmetic.code.push_back({dao::Opcode::AddI64, 0, 2, 0, 1, 0});
    }
    arithmetic.code.push_back({dao::Opcode::Return, 0, 0, 2, 0, 0});
    const uint32_t arithmetic_index = builder.add_function(std::move(arithmetic));
    builder.add_export(2, arithmetic_index);
    const auto bytes = builder.encode();

    dao_vm* vm = dao_vm_create(nullptr);
    if (vm == nullptr)
        return EXIT_FAILURE;
    dao_error error{};

    const auto load_start = Clock::now();
    for (uint64_t index = 0; index < load_iterations; ++index) {
        dao_module* loaded = nullptr;
        if (dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &loaded, &error) != DAO_OK)
            return EXIT_FAILURE;
        dao_module_release(loaded);
    }
    const auto load_end = Clock::now();

    dao_module* module = nullptr;
    if (dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) != DAO_OK)
        return EXIT_FAILURE;
    dao_function function = 0;
    if (dao_module_find_export(module, 1, &function) != DAO_OK)
        return EXIT_FAILURE;

    const dao_value args[] = {{DAO_VALUE_I64, 0, 40}, {DAO_VALUE_I64, 0, 2}};
    dao_value result{};
    for (uint32_t index = 0; index < 1'000; ++index) {
        if (dao_vm_call(vm, module, function, args, 2, &result, &error) != DAO_OK)
            return EXIT_FAILURE;
    }

    const auto call_start = Clock::now();
    for (uint64_t index = 0; index < call_iterations; ++index) {
        if (dao_vm_call(vm, module, function, args, 2, &result, &error) != DAO_OK)
            return EXIT_FAILURE;
    }
    const auto call_end = Clock::now();
    if (result.payload != 42)
        return EXIT_FAILURE;

    const double load_ns = elapsed_ns(load_start, load_end) / static_cast<double>(load_iterations);
    const double call_ns = elapsed_ns(call_start, call_end) / static_cast<double>(call_iterations);
    const double calls_per_second = 1'000'000'000.0 / call_ns;

    dao::ModuleBuilder host_builder;
    const uint32_t host_import = host_builder.add_import(1000, 2);
    dao::FunctionSpec host_wrapper;
    host_wrapper.parameter_count = 2;
    host_wrapper.register_count = 3;
    host_wrapper.code = {
        {dao::Opcode::CallHost, 0, 2, 0, 2, host_import},
        {dao::Opcode::Return, 0, 0, 2, 0, 0},
    };
    const uint32_t host_wrapper_index = host_builder.add_function(std::move(host_wrapper));
    host_builder.add_export(3, host_wrapper_index);
    const auto host_bytes = host_builder.encode();

    dao_host_function host{sizeof(dao_host_function), 1000, 2, 0, host_add, nullptr};
    if (dao_vm_register_host_function(vm, &host) != DAO_OK)
        return EXIT_FAILURE;
    dao_module* host_module = nullptr;
    if (dao_vm_load_module(vm, {host_bytes.data(), host_bytes.size()}, &host_module, &error) !=
        DAO_OK) {
        return EXIT_FAILURE;
    }
    dao_function host_function = 0;
    if (dao_module_find_export(host_module, 3, &host_function) != DAO_OK)
        return EXIT_FAILURE;
    const auto host_start = Clock::now();
    for (uint64_t index = 0; index < call_iterations; ++index) {
        if (dao_vm_call(vm, host_module, host_function, args, 2, &result, &error) != DAO_OK)
            return EXIT_FAILURE;
    }
    const auto host_end = Clock::now();
    const double host_call_ns =
        elapsed_ns(host_start, host_end) / static_cast<double>(call_iterations);

    dao_function arithmetic_function = 0;
    if (dao_module_find_export(module, 2, &arithmetic_function) != DAO_OK)
        return EXIT_FAILURE;
    const uint64_t arithmetic_iterations = std::max<uint64_t>(1'000, call_iterations / 10);
    const auto arithmetic_start = Clock::now();
    for (uint64_t index = 0; index < arithmetic_iterations; ++index) {
        if (dao_vm_call(vm, module, arithmetic_function, args, 2, &result, &error) != DAO_OK) {
            return EXIT_FAILURE;
        }
    }
    const auto arithmetic_end = Clock::now();
    const double arithmetic_ns = elapsed_ns(arithmetic_start, arithmetic_end);
    const double typed_ops_per_second = static_cast<double>(arithmetic_iterations) *
                                        arithmetic_ops_per_call * 1'000'000'000.0 / arithmetic_ns;

    std::cout << std::fixed << std::setprecision(2) << "module_bytes=" << bytes.size() << '\n'
              << "load_ns=" << load_ns << '\n'
              << "call_ns=" << call_ns << '\n'
              << "host_call_ns=" << host_call_ns << '\n'
              << "calls_per_second=" << calls_per_second << '\n'
              << "typed_ops_per_second=" << typed_ops_per_second << '\n';

    dao_module_release(module);
    dao_module_release(host_module);
    dao_vm_destroy(vm);
    return EXIT_SUCCESS;
}
