#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
uint32_t symbol_id(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) { hash ^= byte; hash *= 16777619u; }
    return hash;
}
bool resolve_error(void* user_data, std::string_view path, std::string* source, std::string* error) {
    if (path != "std/error" && path != "error") { *error = "unknown module"; return false; }
    *source = *static_cast<std::string*>(user_data);
    return true;
}
bool call(dao_vm* vm, dao_module* module, const char* name, dao_value* result, dao_error* error) {
    dao_function function{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, result, error) == DAO_OK;
}
bool string_equals(const dao_value& value, std::string_view expected) {
    dao_bytes bytes{};
    return dao_value_get_view(&value, &bytes) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size) == expected;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string error_source(std::istreambuf_iterator<char>(input), {});
    if (!input && error_source.empty()) return EXIT_FAILURE;
    const char* program = R"(
import "std/error" as error
thought make_case() { error_make("NOT_FOUND", "missing") }
thought check_case() { error_is_error(error_make("BAD", "invalid")) }
thought code_case() { error_code(error_make("BAD", "invalid")) }
thought message_case() { error_message(error_make("BAD", "invalid")) }
thought field_case() { error_make("BAD", "invalid").message }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_error;
    options.import_user_data = &error_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile std/error integration");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(vm != nullptr && dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load std/error integration");
    if (module != nullptr) {
        dao_value result{};
        check(call(vm, module, "make_case", &result, &error) && result.type == DAO_VALUE_MAP, "make error map");
        check(call(vm, module, "check_case", &result, &error) && result.type == DAO_VALUE_TRIT && result.payload == 1, "error predicate");
        check(call(vm, module, "code_case", &result, &error) && string_equals(result, "BAD"), "error code");
        check(call(vm, module, "message_case", &result, &error) && string_equals(result, "invalid"), "error message");
        check(call(vm, module, "field_case", &result, &error) && string_equals(result, "invalid"), "error field access");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated error tests passed\n";
    return EXIT_SUCCESS;
}
