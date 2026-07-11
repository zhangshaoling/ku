#include "dao/ku_migration.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
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

enum class Operation { Length, Trim, Upper, Lower, Replace, Contains, Starts, Ends, Substring, CharAt, Concat };

struct StringHost;
struct CallbackContext { StringHost* host; Operation operation; };
struct StringHost { std::deque<std::string> results; };

bool string_arg(const dao_value& value, std::string* out) {
    dao_bytes bytes{};
    if (dao_value_get_view(&value, &bytes) != DAO_OK) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    return true;
}

dao_status return_string(StringHost* host, std::string value, dao_value* out) {
    host->results.push_back(std::move(value));
    const auto& result = host->results.back();
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(result.data()), result.size()}, out);
}

dao_status string_callback(void* user_data, const dao_value* args, size_t count,
                           dao_value* out) {
    auto* context = static_cast<CallbackContext*>(user_data);
    std::string value;
    if (count == 0 || !string_arg(args[0], &value)) return DAO_TYPE_ERROR;
    const auto trit = [&](bool truth) {
        *out = {DAO_VALUE_TRIT, 0, truth ? 1 : -1};
        return DAO_OK;
    };
    switch (context->operation) {
    case Operation::Length:
        if (count != 1) return DAO_INVALID_ARGUMENT;
        *out = {DAO_VALUE_I64, 0, static_cast<int64_t>(value.size())};
        return DAO_OK;
    case Operation::Trim: {
        if (count != 1) return DAO_INVALID_ARGUMENT;
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return return_string(context->host, {}, out);
        const auto last = value.find_last_not_of(" \t\r\n");
        return return_string(context->host, value.substr(first, last - first + 1), out);
    }
    case Operation::Upper:
    case Operation::Lower:
        if (count != 1) return DAO_INVALID_ARGUMENT;
        std::transform(value.begin(), value.end(), value.begin(), [&](unsigned char byte) {
            return static_cast<char>(context->operation == Operation::Upper
                                         ? std::toupper(byte)
                                         : std::tolower(byte));
        });
        return return_string(context->host, std::move(value), out);
    case Operation::Replace: {
        std::string old;
        std::string replacement;
        if (count != 3 || !string_arg(args[1], &old) || !string_arg(args[2], &replacement))
            return DAO_TYPE_ERROR;
        if (!old.empty()) {
            size_t position = 0;
            while ((position = value.find(old, position)) != std::string::npos) {
                value.replace(position, old.size(), replacement);
                position += replacement.size();
            }
        }
        return return_string(context->host, std::move(value), out);
    }
    case Operation::Contains:
    case Operation::Starts:
    case Operation::Ends: {
        std::string part;
        if (count != 2 || !string_arg(args[1], &part)) return DAO_TYPE_ERROR;
        if (context->operation == Operation::Contains) return trit(value.find(part) != std::string::npos);
        if (context->operation == Operation::Starts) return trit(value.starts_with(part));
        return trit(value.ends_with(part));
    }
    case Operation::Substring: {
        if (count != 3 || args[1].type != DAO_VALUE_I64 || args[2].type != DAO_VALUE_I64)
            return DAO_TYPE_ERROR;
        const int64_t size = static_cast<int64_t>(value.size());
        const int64_t start = std::clamp<int64_t>(args[1].payload, 0, size);
        const int64_t end = std::clamp<int64_t>(args[2].payload, start, size);
        return return_string(context->host,
                             value.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)), out);
    }
    case Operation::CharAt:
        if (count != 2 || args[1].type != DAO_VALUE_I64 || args[1].payload < 0 ||
            static_cast<uint64_t>(args[1].payload) >= value.size()) return DAO_INVALID_ARGUMENT;
        return return_string(context->host,
                             value.substr(static_cast<size_t>(args[1].payload), 1), out);
    case Operation::Concat: {
        std::string right;
        if (count != 2 || !string_arg(args[1], &right)) return DAO_TYPE_ERROR;
        return return_string(context->host, value + right, out);
    }
    }
    return DAO_RUNTIME_ERROR;
}

bool resolve_string(void* user_data, std::string_view path, std::string* source,
                    std::string* error) {
    if (path != "std/string") {
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

bool equals_string(const dao_value& value, std::string_view expected) {
    dao_bytes bytes{};
    return dao_value_get_view(&value, &bytes) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size) == expected;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string string_source(std::istreambuf_iterator<char>(input), {});
    if (!input && string_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/string" as string
thought empty_case() { string_is_empty("") }
thought length_case() { string_length("dao") }
thought not_empty_case() { string_not_empty("dao") }
thought trim_case() { string_trim("  dao \n") }
thought upper_case() { string_upper("Dao42") }
thought lower_case() { string_lower("DAO42") }
thought replace_case() { string_replace("a-b-a", "a", "x") }
thought contains_case() { string_contains("dao kernel", "kernel") }
thought starts_case() { string_starts_with("dao kernel", "dao") }
thought ends_case() { string_ends_with("dao kernel", "kernel") }
thought substring_case() { string_substring("abcdef", 1, 4) }
thought char_case() { string_char_at("abc", 1) }
thought concat_case() { string_concat("dao", " kernel") }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_string;
    options.import_user_data = &string_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated string");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated string");

    StringHost host{};
    CallbackContext contexts[] = {
        {&host, Operation::Length}, {&host, Operation::Trim}, {&host, Operation::Upper},
        {&host, Operation::Lower}, {&host, Operation::Replace}, {&host, Operation::Contains},
        {&host, Operation::Starts}, {&host, Operation::Ends}, {&host, Operation::Substring},
        {&host, Operation::CharAt},
        {&host, Operation::Concat},
    };
    const char* names[] = {"host_string_length", "host_string_trim", "host_string_upper",
                           "host_string_lower", "host_string_replace", "host_string_contains",
                           "host_string_starts_with", "host_string_ends_with",
                           "host_string_substring", "host_string_char_at",
                           "host_string_concat"};
    const uint32_t arities[] = {1, 1, 1, 1, 3, 2, 2, 2, 3, 2, 2};
    for (size_t index = 0; index < std::size(contexts); ++index) {
        const dao_host_function function{sizeof(dao_host_function), symbol_id(names[index]),
                                         arities[index], 0, string_callback, &contexts[index]};
        check(dao_vm_register_host_function(vm, &function) == DAO_OK, "register string host");
    }

    if (module != nullptr) {
        dao_value result{};
        check(call(vm, module, "empty_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1, "string empty");
        check(call(vm, module, "length_case", &result, &error) &&
                  result.type == DAO_VALUE_I64 && result.payload == 3, "string length");
        check(call(vm, module, "not_empty_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1, "string not empty");
        check(call(vm, module, "trim_case", &result, &error) && equals_string(result, "dao"),
              "string trim");
        check(call(vm, module, "upper_case", &result, &error) && equals_string(result, "DAO42"),
              "string upper");
        check(call(vm, module, "lower_case", &result, &error) && equals_string(result, "dao42"),
              "string lower");
        check(call(vm, module, "replace_case", &result, &error) && equals_string(result, "x-b-x"),
              "string replace");
        check(call(vm, module, "contains_case", &result, &error) && result.payload == 1,
              "string contains");
        check(call(vm, module, "starts_case", &result, &error) && result.payload == 1,
              "string starts with");
        check(call(vm, module, "ends_case", &result, &error) && result.payload == 1,
              "string ends with");
        check(call(vm, module, "substring_case", &result, &error) && equals_string(result, "bcd"),
              "string substring");
        check(call(vm, module, "char_case", &result, &error) && equals_string(result, "b"),
              "string char at");
        check(call(vm, module, "concat_case", &result, &error) &&
                  equals_string(result, "dao kernel"), "string concat");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated string tests passed\n";
    return EXIT_SUCCESS;
}
