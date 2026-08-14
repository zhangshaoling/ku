#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}

uint32_t symbol_id(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) { hash ^= byte; hash *= 16777619u; }
    return hash;
}

bool resolve_queue(void* user_data, std::string_view path, std::string* source,
                   std::string* error) {
    if (path != "task_queue") { *error = "unknown module"; return false; }
    *source = *static_cast<std::string*>(user_data);
    return true;
}

dao_status static_string(std::string_view value, dao_value* out) {
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(value.data()), value.size()}, out);
}

dao_status task_db_path(void*, const dao_value*, size_t count, dao_value* out) {
    return count == 0 ? static_string("C:/dao/tasks.db", out) : DAO_INVALID_ARGUMENT;
}

dao_status datetime_now(void*, const dao_value*, size_t count, dao_value* out) {
    return count == 0 ? static_string("2026-07-12 12:00:00", out) : DAO_INVALID_ARGUMENT;
}

dao_status sqlite_open(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type != DAO_VALUE_STRING) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_I64, 0, 1};
    return DAO_OK;
}

dao_status sqlite_query(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* vm = static_cast<dao_vm*>(user_data);
    if (count != 3 || args[0].type != DAO_VALUE_I64 || args[1].type != DAO_VALUE_STRING ||
        args[2].type != DAO_VALUE_LIST || dao_vm_make_list(vm, out) != DAO_OK)
        return DAO_TYPE_ERROR;
    dao_value task{};
    dao_value id{};
    const uint8_t key[] = {'i','d'};
    if (dao_vm_make_map(vm, &task) != DAO_OK || static_string("task-1", &id) != DAO_OK ||
        dao_value_map_set(vm, &task, {key, sizeof(key)}, &id) != DAO_OK ||
        dao_value_list_append(vm, out, &task) != DAO_OK)
        return DAO_RUNTIME_ERROR;
    return DAO_OK;
}

dao_status sqlite_exec(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 3 || args[0].type != DAO_VALUE_I64 || args[1].type != DAO_VALUE_STRING ||
        args[2].type != DAO_VALUE_LIST) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_I64, 0, 1};
    return DAO_OK;
}

dao_status sqlite_close(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type != DAO_VALUE_I64) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

bool map_string(dao_vm* vm, const dao_value& map, std::string_view key,
                std::string_view expected) {
    dao_value value{};
    dao_bytes bytes{};
    return dao_value_map_get(
               vm, &map, {reinterpret_cast<const uint8_t*>(key.data()), key.size()}, &value) == DAO_OK &&
           value.type == DAO_VALUE_STRING && dao_value_get_view(&value, &bytes) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size) == expected;
}

bool call_route(dao_vm* vm, dao_module* module, const char* name, std::string_view mode,
                std::string_view provider, std::string_view model, dao_error* error) {
    dao_function function{};
    dao_value result{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == DAO_VALUE_MAP && map_string(vm, result, "mode", mode) &&
           map_string(vm, result, "provider", provider) && map_string(vm, result, "model", model);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string queue_source(std::istreambuf_iterator<char>(input), {});
    if (!input && queue_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "task_queue" as queue
thought search_case() { queue_routing_suggestion("search", null) }
thought file_case() { queue_routing_suggestion("file_ops", null) }
thought code_case() { queue_routing_suggestion("code_analysis", "review") }
thought monitor_case() { queue_routing_suggestion("monitoring", "health") }
thought reasoning_case() { queue_routing_suggestion("reasoning", "solve") }
thought unknown_case() { queue_routing_suggestion("unknown", null) }
thought claim_case() { queue_claim_next() }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_queue;
    options.import_user_data = &queue_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    const bool compiled = dao::km::compile(program, builder, &error, options);
    if (!compiled) std::cerr << "task queue test compile error: " << error.message << '\n';
    check(compiled, "compile task queue route fixture");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("host_task_db_path"), 0, 0, task_db_path, vm},
        {sizeof(dao_host_function), symbol_id("host_datetime_now"), 0, 0, datetime_now, vm},
        {sizeof(dao_host_function), symbol_id("host_sqlite_open"), 1, 0, sqlite_open, vm},
        {sizeof(dao_host_function), symbol_id("host_sqlite_query"), 3, 0, sqlite_query, vm},
        {sizeof(dao_host_function), symbol_id("host_sqlite_exec"), 3, 0, sqlite_exec, vm},
        {sizeof(dao_host_function), symbol_id("host_sqlite_close"), 1, 0, sqlite_close, vm}};
    for (const auto& host : hosts)
        check(dao_vm_register_host_function(vm, &host) == DAO_OK, "register task queue host");
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load task queue route fixture");
    if (module != nullptr) {
        check(call_route(vm, module, "search_case", "mcp_tool", "scrapling", "", &error),
              "search route");
        check(call_route(vm, module, "file_case", "direct", "local", "", &error),
              "file route");
        check(call_route(vm, module, "code_case", "agent", "ollama-hubei-llm", "qwen2.5:7b", &error),
              "code route");
        check(call_route(vm, module, "monitor_case", "agent", "windows-ollama", "llama3.2:3b", &error),
              "monitor route");
        check(call_route(vm, module, "reasoning_case", "agent", "deepseek", "deepseek-v4-flash", &error),
              "reasoning route");
        check(call_route(vm, module, "unknown_case", "agent", "deepseek", "deepseek-v4-flash", &error),
              "fallback route");
        dao_function claim{};
        dao_value task{};
        check(dao_module_find_export(module, symbol_id("claim_case"), &claim) == DAO_OK &&
                  dao_vm_call(vm, module, claim, nullptr, 0, &task, &error) == DAO_OK &&
                  task.type == DAO_VALUE_MAP && map_string(vm, task, "id", "task-1"),
              "claim task from Host-created query rows");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao legacy task queue route tests passed\n";
    return EXIT_SUCCESS;
}
