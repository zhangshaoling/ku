#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << name << '\n';
    }
}

uint32_t symbol_id(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

struct DebugHost {
    int64_t clock = 100;
    std::vector<std::string> logs;
};

dao_status debug_log(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1) return DAO_INVALID_ARGUMENT;
    auto* host = static_cast<DebugHost*>(user_data);
    if (args[0].type == DAO_VALUE_I64) {
        host->logs.push_back(std::to_string(args[0].payload));
    } else if (args[0].type == DAO_VALUE_STRING) {
        dao_bytes bytes{};
        if (dao_value_get_view(&args[0], &bytes) != DAO_OK) return DAO_TYPE_ERROR;
        host->logs.emplace_back(reinterpret_cast<const char*>(bytes.data), bytes.size);
    } else {
        return DAO_TYPE_ERROR;
    }
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status monotonic_now(void* user_data, const dao_value*, size_t count, dao_value* out) {
    if (count != 0) return DAO_INVALID_ARGUMENT;
    auto* host = static_cast<DebugHost*>(user_data);
    *out = {DAO_VALUE_I64, 0, host->clock};
    host->clock += 7;
    return DAO_OK;
}

dao_status memory_current(void*, const dao_value*, size_t count, dao_value* out) {
    if (count != 0) return DAO_INVALID_ARGUMENT;
    *out = {DAO_VALUE_I64, 0, 4096};
    return DAO_OK;
}

bool resolve_debug(void* user_data, std::string_view path, std::string* source,
                   std::string* error) {
    if (path != "std/debug") {
        *error = "unknown module";
        return false;
    }
    *source = *static_cast<std::string*>(user_data);
    return true;
}

bool call(dao_vm* vm, dao_module* module, const char* name, dao_value* result,
          dao_error* error) {
    dao_function function{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, result, error) == DAO_OK;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string debug_source(std::istreambuf_iterator<char>(input), {});
    if (!input && debug_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/debug" as debug
thought trace_case() { debug_trace("hello") }
thought timer_start_case() { debug_timer_start() }
thought timer_elapsed_case() { debug_timer_elapsed(100) }
thought work() { 42 }
thought measure_case() { debug_measure(work) }
thought assert_true_case() { debug_assert(true, "unused") }
thought assert_false_case() { debug_assert(false, "bad") }
thought assert_eq_true_case() { debug_assert_eq(42, 42) }
thought assert_eq_false_case() { debug_assert_eq(41, 42) }
thought memory_case() { debug_memory_usage() }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_debug;
    options.import_user_data = &debug_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated debug");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated debug");

    DebugHost host{};
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("debug_log"), 1, 0, debug_log, &host},
        {sizeof(dao_host_function), symbol_id("monotonic_now"), 0, 0, monotonic_now, &host},
        {sizeof(dao_host_function), symbol_id("memory_current"), 0, 0, memory_current, &host},
    };
    for (const auto& function : hosts)
        check(dao_vm_register_host_function(vm, &function) == DAO_OK, "register debug host");

    if (module != nullptr) {
        dao_value result{};
        check(call(vm, module, "trace_case", &result, &error) && host.logs == std::vector<std::string>{"hello"},
              "debug trace");
        check(call(vm, module, "timer_start_case", &result, &error) &&
                  result.type == DAO_VALUE_I64 && result.payload == 100,
              "debug timer start");
        check(call(vm, module, "timer_elapsed_case", &result, &error) &&
                  result.type == DAO_VALUE_I64 && result.payload == 7,
              "debug timer elapsed");
        check(call(vm, module, "measure_case", &result, &error) && result.type == DAO_VALUE_MAP,
              "debug measure");
        const uint8_t result_key[] = {'r','e','s','u','l','t'};
        const uint8_t elapsed_key[] = {'e','l','a','p','s','e','d'};
        dao_value measured_result{};
        dao_value measured_elapsed{};
        check(dao_value_map_get(vm, &result, {result_key, sizeof(result_key)}, &measured_result) == DAO_OK &&
                  dao_value_map_get(vm, &result, {elapsed_key, sizeof(elapsed_key)}, &measured_elapsed) == DAO_OK &&
                  measured_result.type == DAO_VALUE_I64 && measured_result.payload == 42 &&
                  measured_elapsed.type == DAO_VALUE_I64 && measured_elapsed.payload == 7,
              "debug measured values");
        check(call(vm, module, "assert_true_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "debug assert true");
        check(call(vm, module, "assert_false_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == -1 && host.logs.back() == "bad",
              "debug assert false");
        check(call(vm, module, "assert_eq_true_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "debug assert equal");
        check(call(vm, module, "assert_eq_false_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == -1 &&
                  host.logs[host.logs.size() - 2] == "41" && host.logs.back() == "42",
              "debug assert unequal");
        check(call(vm, module, "memory_case", &result, &error) &&
                  result.type == DAO_VALUE_I64 && result.payload == 4096,
              "debug memory usage");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated debug tests passed\n";
    return EXIT_SUCCESS;
}
