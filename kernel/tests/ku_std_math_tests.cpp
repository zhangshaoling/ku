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

bool resolve_math(void* user_data, std::string_view path, std::string* source, std::string* error) {
    if (path != "std/math") {
        *error = "unknown module";
        return false;
    }
    *source = static_cast<Source*>(user_data)->value;
    return true;
}

bool expect_i64(dao_vm* vm, dao_module* module, const char* name, int64_t expected,
                dao_error* error) {
    dao_function function{};
    dao_value result{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, &result, error) == DAO_OK &&
           result.type == DAO_VALUE_I64 && result.payload == expected;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    Source math{std::string(std::istreambuf_iterator<char>(input), {})};
    if (!input && math.value.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/math" as math
thought sum_case() { math_sum([1, 2, 3, 4]) }
thought product_case() { math_product([1, 2, 3, 4]) }
thought avg_case() { math_avg([1, 2, 3, 4]) }
thought clamp_case() { math_clamp(9, 2, 7) }
thought lerp_case() { math_lerp(10, 20, 3) }
thought gcd_case() { math_gcd(54, 24) }
thought lcm_case() { math_lcm(6, 7) }
thought prime_case() { math_is_prime(97) }
thought fibonacci_case() { math_fibonacci(10) }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_math;
    options.import_user_data = &math;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated math");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated math");
    if (module != nullptr) {
        check(expect_i64(vm, module, "sum_case", 10, &error), "math sum");
        check(expect_i64(vm, module, "product_case", 24, &error), "math product");
        check(expect_i64(vm, module, "avg_case", 2, &error), "math avg");
        check(expect_i64(vm, module, "clamp_case", 7, &error), "math clamp");
        check(expect_i64(vm, module, "lerp_case", 40, &error), "math lerp");
        check(expect_i64(vm, module, "gcd_case", 6, &error), "math gcd");
        check(expect_i64(vm, module, "lcm_case", 42, &error), "math lcm");
        check(expect_i64(vm, module, "fibonacci_case", 55, &error), "math fibonacci");
        dao_function prime{};
        dao_value result{};
        check(dao_module_find_export(module, symbol_id("prime_case"), &prime) == DAO_OK &&
                  dao_vm_call(vm, module, prime, nullptr, 0, &result, &error) == DAO_OK &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "math prime");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated math tests passed\n";
    return EXIT_SUCCESS;
}
