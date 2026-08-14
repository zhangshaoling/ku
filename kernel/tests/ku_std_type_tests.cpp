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

bool resolve_type(void* user_data, std::string_view path, std::string* source,
                  std::string* error) {
    if (path != "std/type") { *error = "unknown module"; return false; }
    *source = *static_cast<std::string*>(user_data);
    return true;
}

dao_status value_type(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type > DAO_VALUE_CLOSURE) return DAO_TYPE_ERROR;
    static constexpr std::string_view names[] = {
        "null", "i64", "trit", "bytes", "string", "list", "map", "function", "closure"};
    const auto name = names[args[0].type];
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(name.data()), name.size()}, out);
}

dao_status make_bytes(void*, const dao_value*, size_t count, dao_value* out) {
    static constexpr uint8_t bytes[] = {0x44, 0x41, 0x4f};
    if (count != 0) return DAO_INVALID_ARGUMENT;
    return dao_value_make_bytes_view({bytes, sizeof(bytes)}, out);
}

bool call_string(dao_vm* vm, dao_module* module, const char* export_name,
                 std::string_view expected, dao_error* error) {
    dao_function function{};
    dao_value result{};
    dao_bytes text{};
    return dao_module_find_export(module, symbol_id(export_name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == DAO_VALUE_STRING && dao_value_get_view(&result, &text) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(text.data), text.size) == expected;
}

bool call_trit(dao_vm* vm, dao_module* module, const char* export_name, dao_error* error) {
    dao_function function{};
    dao_value result{};
    return dao_module_find_export(module, symbol_id(export_name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == DAO_VALUE_TRIT && result.payload == 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string type_source(std::istreambuf_iterator<char>(input), {});
    if (!input && type_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/type" as type
import host_test_bytes(0)
thought identity(value) { value }
thought null_case() { type_type_of(null) }
thought i64_case() { type_type_of(42) }
thought trit_case() { type_type_of(true) }
thought bytes_case() { type_type_of(host_test_bytes()) }
thought string_case() { type_type_of("dao") }
thought list_case() { type_type_of([1, 2]) }
thought map_case() { type_type_of({"answer": 42}) }
thought function_case() { type_type_of(identity) }
thought closure_case() { type_type_of(bind(identity, 42)) }
thought predicates_case() {
  type_is_null(null) and type_is_num(42) and type_is_string("dao") and type_is_list([]) and type_is_map({})
}
)";
    dao::km::Options options{};
    options.import_resolver = resolve_type;
    options.import_user_data = &type_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    const bool compiled = dao::km::compile(program, builder, &error, options);
    if (!compiled) std::cerr << "type compile error: " << error.message << '\n';
    check(compiled, "compile migrated type");
    const auto module_bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("host_value_type"), 1, 0, value_type, nullptr},
        {sizeof(dao_host_function), symbol_id("host_test_bytes"), 0, 0, make_bytes, nullptr}};
    for (const auto& host : hosts)
        check(dao_vm_register_host_function(vm, &host) == DAO_OK, "register type host");
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {module_bytes.data(), module_bytes.size()}, &module, &error) == DAO_OK,
          "load migrated type");
    if (module != nullptr) {
        const struct { const char* function; const char* expected; } cases[] = {
            {"null_case", "null"}, {"i64_case", "i64"}, {"trit_case", "trit"},
            {"bytes_case", "bytes"}, {"string_case", "string"}, {"list_case", "list"},
            {"map_case", "map"}, {"function_case", "function"}, {"closure_case", "closure"}};
        for (const auto& test : cases)
            check(call_string(vm, module, test.function, test.expected, &error), test.function);
        check(call_trit(vm, module, "predicates_case", &error), "type predicates");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated type tests passed\n";
    return EXIT_SUCCESS;
}
