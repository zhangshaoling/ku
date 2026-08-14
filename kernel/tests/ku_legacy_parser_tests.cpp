#include "dao/ku_migration.hpp"

#include <charconv>
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

bool resolve_parser(void* user_data, std::string_view path, std::string* source,
                    std::string* error) {
    if (path != "parser") { *error = "unknown module"; return false; }
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

dao_status string_to_i64(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type != DAO_VALUE_STRING) return DAO_TYPE_ERROR;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[0], &bytes) != DAO_OK) return DAO_TYPE_ERROR;
    const char* begin = reinterpret_cast<const char*>(bytes.data);
    const char* end = begin + bytes.size;
    int64_t value = 0;
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return DAO_INVALID_ARGUMENT;
    *out = {DAO_VALUE_I64, 0, value};
    return DAO_OK;
}

bool map_get(dao_vm* vm, const dao_value& map, std::string_view key, dao_value* out) {
    return dao_value_map_get(
               vm, &map,
               {reinterpret_cast<const uint8_t*>(key.data()), key.size()}, out) == DAO_OK;
}

bool string_equals(const dao_value& value, std::string_view expected) {
    dao_bytes bytes{};
    return value.type == DAO_VALUE_STRING && dao_value_get_view(&value, &bytes) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size) == expected;
}

bool node_is(dao_vm* vm, const dao_value& node, std::string_view type,
             std::string_view value = {}) {
    dao_value actual_type{};
    dao_value actual_value{};
    return node.type == DAO_VALUE_MAP && map_get(vm, node, "type", &actual_type) &&
           map_get(vm, node, "value", &actual_value) && string_equals(actual_type, type) &&
           (value.empty() || string_equals(actual_value, value));
}

bool child(dao_vm* vm, const dao_value& node, size_t index, dao_value* out) {
    dao_value children{};
    return map_get(vm, node, "children", &children) &&
           dao_value_list_get(vm, &children, index, out) == DAO_OK;
}

bool call(dao_vm* vm, dao_module* module, const char* name, dao_value* out, dao_error* error) {
    dao_function function{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, out, error) == DAO_OK;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string parser_source(std::istreambuf_iterator<char>(input), {});
    if (!input && parser_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "parser" as parser
thought token(type, value) { {"type": type, "value": value} }
thought assignment_case() {
  parser_parse_tokens([
    token("name", "answer"), token("op", "="), token("number", "40"),
    token("op", "+"), token("number", "2"), token("eof", "")
  ])
}
thought index_assignment_case() {
  parser_parse_tokens([
    token("name", "items"), token("punct", "["), token("number", "0"),
    token("punct", "]"), token("op", "="), token("number", "42"), token("eof", "")
  ])
}
)";
    dao::km::Options options{};
    options.import_resolver = resolve_parser;
    options.import_user_data = &parser_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    const bool compiled = dao::km::compile(program, builder, &error, options);
    if (!compiled) std::cerr << "legacy parser test compile error: " << error.message << '\n';
    check(compiled, "compile legacy parser execution fixture");

    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("host_value_type"), 1, 0, value_type, nullptr},
        {sizeof(dao_host_function), symbol_id("host_string_to_i64"), 1, 0, string_to_i64, nullptr}};
    for (const auto& host : hosts)
        check(dao_vm_register_host_function(vm, &host) == DAO_OK, "register parser host");
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load legacy parser execution fixture");
    if (module != nullptr) {
        dao_value root{};
        dao_value statement{};
        dao_value expression{};
        dao_value left{};
        dao_value right{};
        check(call(vm, module, "assignment_case", &root, &error) &&
                  node_is(vm, root, "block") && child(vm, root, 0, &statement) &&
                  node_is(vm, statement, "assign", "answer") &&
                  child(vm, statement, 0, &expression) && node_is(vm, expression, "op", "+") &&
                  child(vm, expression, 0, &left) && node_is(vm, left, "literal") &&
                  map_get(vm, left, "value", &left) && left.type == DAO_VALUE_I64 &&
                  left.payload == 40 && child(vm, expression, 1, &right) &&
                  node_is(vm, right, "literal") && map_get(vm, right, "value", &right) &&
                  right.type == DAO_VALUE_I64 && right.payload == 2,
              "execute assignment parser path");

        dao_value index_node{};
        check(call(vm, module, "index_assignment_case", &root, &error) &&
                  node_is(vm, root, "block") && child(vm, root, 0, &statement) &&
                  node_is(vm, statement, "index_assign") && child(vm, statement, 0, &index_node) &&
                  node_is(vm, index_node, "index") && child(vm, index_node, 0, &left) &&
                  node_is(vm, left, "ref", "items") && child(vm, index_node, 1, &right) &&
                  node_is(vm, right, "literal") && map_get(vm, right, "value", &right) &&
                  right.type == DAO_VALUE_I64 && right.payload == 0 &&
                  child(vm, statement, 1, &right) && node_is(vm, right, "literal") &&
                  map_get(vm, right, "value", &right) && right.type == DAO_VALUE_I64 &&
                  right.payload == 42,
              "execute index assignment parser path");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao legacy parser execution tests passed\n";
    return EXIT_SUCCESS;
}
