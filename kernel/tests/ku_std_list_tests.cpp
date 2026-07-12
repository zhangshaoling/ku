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

struct Sources { std::string list; std::string type; };

bool resolve_list(void* user_data, std::string_view path, std::string* source, std::string* error) {
    const auto* sources = static_cast<Sources*>(user_data);
    if (path == "std/list") *source = sources->list;
    else if (path == "type") *source = sources->type;
    else { *error = "unknown module"; return false; }
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

bool expect_value(dao_vm* vm, dao_module* module, const char* name, dao_value_type type,
                  int64_t expected, dao_error* error) {
    dao_function function{};
    dao_value result{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == type && result.payload == expected;
}

bool expect_list_i64(dao_vm* vm, dao_module* module, const char* name,
                     const std::vector<int64_t>& expected, dao_error* error) {
    dao_function function{};
    dao_value result{};
    if (dao_module_find_export(module, symbol_id(name), &function) != DAO_OK ||
        dao_vm_call(vm, module, function, nullptr, 0, &result, error) != DAO_OK ||
        result.type != DAO_VALUE_LIST) {
        return false;
    }
    size_t size = 0;
    if (dao_value_list_size(vm, &result, &size) != DAO_OK || size != expected.size())
        return false;
    for (size_t index = 0; index < size; ++index) {
        dao_value item{};
        if (dao_value_list_get(vm, &result, index, &item) != DAO_OK ||
            item.type != DAO_VALUE_I64 || item.payload != expected[index]) {
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) return EXIT_FAILURE;
    std::ifstream list_input(argv[1], std::ios::binary);
    std::ifstream type_input(argv[2], std::ios::binary);
    Sources sources{std::string(std::istreambuf_iterator<char>(list_input), {}),
                    std::string(std::istreambuf_iterator<char>(type_input), {})};
    if ((!list_input && sources.list.empty()) || (!type_input && sources.type.empty()))
        return EXIT_FAILURE;

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
thought slice_case() { list_slice([3, 1, 4, 1, 5], 1, 4) }
thought reverse_case() { list_reverse([3, 1, 4, 1, 5]) }
thought unique_case() { list_unique([3, 1, 4, 1, 5, 1]) }
thought sort_case() { list_sort([3, 1, 4, 1, 5]) }
thought zip_case() { list_zip([3, 1], [4, 5]) }
thought enumerate_case() { list_enumerate([3, 1]) }
thought square(value) { value * value }
thought even(value) { value % 2 == 0 }
thought add(left, right) { left + right }
thought map_case() { list_map([1, 2, 3], square) }
thought filter_case() { list_filter([1, 2, 3, 4], even) }
thought reduce_case() { list_reduce([1, 2, 3, 4], add, 0) }
thought find_case() { list_find([1, 3, 4, 5], even) }
thought all_case() { list_all([2, 4, 6], even) }
thought any_case() { list_any([1, 3, 4], even) }
thought add_base(base, value) { base + value }
thought bound_map_case() { list_map([1, 2], bind(add_base, 40)) }
thought pair(value) { [value, value + 10] }
thought flat_map_case() { list_flat_map([1, 2], pair) }
thought flatten_case() { list_flatten([1, [2, [3, 4]], [], 5]) }
thought parity_name(value) { if value % 2 == 0 { "even" } else { "odd" } }
thought group_by_case() { list_group_by([1, 2, 3, 4, 5], parity_name) }
thought interleave_case() { list_interleave([1, 3, 5], [2, 4]) }
thought step_case() { list_step([0, 1, 2, 3, 4, 5], 2) }
thought pad_case() { list_pad([1, 2], 5, 9) }
thought rotate_case() { list_rotate([1, 2, 3, 4], 1) }
thought rotate_negative_case() { list_rotate([1, 2, 3, 4], -1) }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_list;
    options.import_user_data = &sources;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated list");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    const dao_host_function type_function{sizeof(dao_host_function), symbol_id("host_value_type"),
                                           1, 0, value_type, nullptr};
    check(dao_vm_register_host_function(vm, &type_function) == DAO_OK,
          "register value type host");
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
        check(expect_list_i64(vm, module, "slice_case", {1, 4, 1}, &error), "list slice");
        check(expect_list_i64(vm, module, "reverse_case", {5, 1, 4, 1, 3}, &error),
              "list reverse");
        check(expect_list_i64(vm, module, "unique_case", {3, 1, 4, 5}, &error),
              "list unique");
        check(expect_list_i64(vm, module, "sort_case", {1, 1, 3, 4, 5}, &error),
              "list sort");
        check(expect_list_i64(vm, module, "map_case", {1, 4, 9}, &error), "list map");
        check(expect_list_i64(vm, module, "filter_case", {2, 4}, &error), "list filter");
        check(expect_value(vm, module, "reduce_case", DAO_VALUE_I64, 10, &error), "list reduce");
        check(expect_value(vm, module, "find_case", DAO_VALUE_I64, 4, &error), "list find");
        check(expect_value(vm, module, "all_case", DAO_VALUE_TRIT, 1, &error), "list all");
        check(expect_value(vm, module, "any_case", DAO_VALUE_TRIT, 1, &error), "list any");
        check(expect_list_i64(vm, module, "bound_map_case", {41, 42}, &error),
              "list bound closure");
        check(expect_list_i64(vm, module, "flat_map_case", {1, 11, 2, 12}, &error),
              "list flat map");
        check(expect_list_i64(vm, module, "flatten_case", {1, 2, 3, 4, 5}, &error),
              "list recursive flatten");
        dao_function group_by{};
        dao_value grouped{};
        dao_value odd{};
        dao_value even{};
        const uint8_t odd_key[]={'o','d','d'};
        const uint8_t even_key[]={'e','v','e','n'};
        size_t group_size=0;
        dao_value group_item{};
        check(dao_module_find_export(module, symbol_id("group_by_case"), &group_by) == DAO_OK &&
                  dao_vm_call(vm, module, group_by, nullptr, 0, &grouped, &error) == DAO_OK &&
                  dao_value_map_get(vm, &grouped, {odd_key, sizeof(odd_key)}, &odd) == DAO_OK &&
                  dao_value_map_get(vm, &grouped, {even_key, sizeof(even_key)}, &even) == DAO_OK &&
                  dao_value_list_size(vm, &odd, &group_size) == DAO_OK && group_size == 3 &&
                  dao_value_list_get(vm, &odd, 2, &group_item) == DAO_OK &&
                  group_item.type == DAO_VALUE_I64 && group_item.payload == 5 &&
                  dao_value_list_size(vm, &even, &group_size) == DAO_OK && group_size == 2 &&
                  dao_value_list_get(vm, &even, 1, &group_item) == DAO_OK &&
                  group_item.type == DAO_VALUE_I64 && group_item.payload == 4,
              "list group by");
        check(expect_list_i64(vm, module, "interleave_case", {1, 2, 3, 4, 5}, &error),
              "list interleave");
        check(expect_list_i64(vm, module, "step_case", {0, 2, 4}, &error), "list step");
        check(expect_list_i64(vm, module, "pad_case", {1, 2, 9, 9, 9}, &error), "list pad");
        check(expect_list_i64(vm, module, "rotate_case", {4, 1, 2, 3}, &error),
              "list rotate");
        check(expect_list_i64(vm, module, "rotate_negative_case", {2, 3, 4, 1}, &error),
              "list negative rotate");
        dao_function zip{};
        dao_value zipped{};
        size_t size = 0;
        dao_value pair{};
        dao_value first{};
        dao_value second{};
        check(dao_module_find_export(module, symbol_id("zip_case"), &zip) == DAO_OK &&
                  dao_vm_call(vm, module, zip, nullptr, 0, &zipped, &error) == DAO_OK &&
                  dao_value_list_size(vm, &zipped, &size) == DAO_OK && size == 2 &&
                  dao_value_list_get(vm, &zipped, 0, &pair) == DAO_OK &&
                  dao_value_list_size(vm, &pair, &size) == DAO_OK && size == 2 &&
                  dao_value_list_get(vm, &pair, 0, &first) == DAO_OK &&
                  dao_value_list_get(vm, &pair, 1, &second) == DAO_OK &&
                  first.type == DAO_VALUE_I64 && first.payload == 3 &&
                  second.type == DAO_VALUE_I64 && second.payload == 4,
              "list zip");
        dao_function enumerate{};
        dao_value enumerated{};
        check(dao_module_find_export(module, symbol_id("enumerate_case"), &enumerate) == DAO_OK &&
                  dao_vm_call(vm, module, enumerate, nullptr, 0, &enumerated, &error) == DAO_OK &&
                  dao_value_list_get(vm, &enumerated, 1, &pair) == DAO_OK &&
                  dao_value_list_size(vm, &pair, &size) == DAO_OK && size == 2 &&
                  dao_value_list_get(vm, &pair, 0, &first) == DAO_OK &&
                  dao_value_list_get(vm, &pair, 1, &second) == DAO_OK &&
                  first.type == DAO_VALUE_I64 && first.payload == 1 &&
                  second.type == DAO_VALUE_I64 && second.payload == 1,
              "list enumerate");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated list tests passed\n";
    return EXIT_SUCCESS;
}
