#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

struct Source { std::string value; };

bool resolve_list(void* user_data, std::string_view path, std::string* source, std::string* error) {
    if (path != "std/list") {
        *error = "unknown module";
        return false;
    }
    *source = static_cast<Source*>(user_data)->value;
    return true;
}

bool expect_value(dao_vm* vm, dao_module* module, const char* name, dao_value_type type,
                  int64_t expected, dao_error* error) {
    dao_function function{};
    dao_value result{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == type && result.payload == expected;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    Source list{std::string(std::istreambuf_iterator<char>(input), {})};
    if (!input && list.value.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/list" as list
thought includes_case() { list_includes([3, 1, 4, 1, 5], 4) }
thought misses_case() { list_includes([3, 1, 4], 2) }
thought first_case() { list_first([3, 1, 4]) }
thought last_case() { list_last([3, 1, 4]) }
thought count_case() { list_count([3, 1, 4, 1, 5], 1) }
thought min_case() { list_min_of([3, 1, 4, 1, 5]) }
thought max_case() { list_max_of([3, 1, 4, 1, 5]) }
thought empty_first_case() { list_first([]) }
thought empty_min_case() { list_min_of([]) }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_list;
    options.import_user_data = &list;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated list");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated list");
    if (module != nullptr) {
        check(expect_value(vm, module, "includes_case", DAO_VALUE_TRIT, 1, &error),
              "list includes");
        check(expect_value(vm, module, "misses_case", DAO_VALUE_TRIT, -1, &error),
              "list misses");
        check(expect_value(vm, module, "first_case", DAO_VALUE_I64, 3, &error), "list first");
        check(expect_value(vm, module, "last_case", DAO_VALUE_I64, 4, &error), "list last");
        check(expect_value(vm, module, "count_case", DAO_VALUE_I64, 2, &error), "list count");
        check(expect_value(vm, module, "min_case", DAO_VALUE_I64, 1, &error), "list min");
        check(expect_value(vm, module, "max_case", DAO_VALUE_I64, 5, &error), "list max");
        check(expect_value(vm, module, "empty_first_case", DAO_VALUE_NULL, 0, &error),
              "list empty first");
        check(expect_value(vm, module, "empty_min_case", DAO_VALUE_NULL, 0, &error),
              "list empty min");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated list tests passed\n";
    return EXIT_SUCCESS;
}
