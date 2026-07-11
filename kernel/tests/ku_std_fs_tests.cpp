#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

struct HostFs {
    std::unordered_map<std::string, std::string> files;
    std::unordered_set<std::string> directories;
};

bool string_arg(const dao_value& value, std::string* out) {
    dao_bytes bytes{};
    if (dao_value_get_view(&value, &bytes) != DAO_OK) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    return true;
}

dao_status path_exists(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    std::string path;
    if (count != 1 || !string_arg(args[0], &path)) return DAO_TYPE_ERROR;
    const auto* fs = static_cast<HostFs*>(user_data);
    const bool exists = fs->files.contains(path) || fs->directories.contains(path);
    *out = {DAO_VALUE_TRIT, 0, exists ? 1 : -1};
    return DAO_OK;
}

dao_status read_file(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    std::string path;
    if (count != 1 || !string_arg(args[0], &path)) return DAO_TYPE_ERROR;
    auto* fs = static_cast<HostFs*>(user_data);
    const auto found = fs->files.find(path);
    if (found == fs->files.end()) return DAO_RUNTIME_ERROR;
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(found->second.data()), found->second.size()}, out);
}

dao_status write_file(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    std::string path;
    std::string content;
    if (count != 2 || !string_arg(args[0], &path) || !string_arg(args[1], &content))
        return DAO_TYPE_ERROR;
    static_cast<HostFs*>(user_data)->files[path] = std::move(content);
    *out = {DAO_VALUE_TRIT, 0, 1};
    return DAO_OK;
}

dao_status make_directory(void* user_data, const dao_value* args, size_t count,
                          dao_value* out) {
    std::string path;
    if (count != 1 || !string_arg(args[0], &path)) return DAO_TYPE_ERROR;
    static_cast<HostFs*>(user_data)->directories.insert(std::move(path));
    *out = {DAO_VALUE_TRIT, 0, 1};
    return DAO_OK;
}

dao_status delete_file(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    std::string path;
    if (count != 1 || !string_arg(args[0], &path)) return DAO_TYPE_ERROR;
    const bool removed = static_cast<HostFs*>(user_data)->files.erase(path) != 0;
    *out = {DAO_VALUE_TRIT, 0, removed ? 1 : -1};
    return DAO_OK;
}

bool resolve_fs(void* user_data, std::string_view path, std::string* source,
                std::string* error) {
    if (path != "std/fs") {
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
    std::string fs_source(std::istreambuf_iterator<char>(input), {});
    if (!input && fs_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/fs" as fs
thought exists_case() { fs_exists("input.txt") }
thought missing_case() { fs_exists("missing.txt") }
thought read_case() { fs_read_text("input.txt") }
thought write_case() { fs_write_text("output.txt", "written") }
thought ensure_case() { fs_ensure_dir("cache") }
thought read_or_case() { fs_read_or("missing.txt", "fallback") }
thought copy_case() { fs_copy_file("input.txt", "copy.txt") }
thought delete_case() { fs_safe_delete("input.txt") }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_fs;
    options.import_user_data = &fs_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated fs");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated fs");

    HostFs fs{{{"input.txt", "payload"}}, {}};
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("path_exists"), 1, 0, path_exists, &fs},
        {sizeof(dao_host_function), symbol_id("read_file"), 1, 0, read_file, &fs},
        {sizeof(dao_host_function), symbol_id("write_file"), 2, 0, write_file, &fs},
        {sizeof(dao_host_function), symbol_id("mkdir"), 1, 0, make_directory, &fs},
        {sizeof(dao_host_function), symbol_id("delete_file"), 1, 0, delete_file, &fs},
    };
    for (const auto& host : hosts)
        check(dao_vm_register_host_function(vm, &host) == DAO_OK, "register fs host");

    if (module != nullptr) {
        dao_value result{};
        check(call(vm, module, "exists_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "fs exists");
        check(call(vm, module, "missing_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == -1,
              "fs missing");
        check(call(vm, module, "read_case", &result, &error) && equals_string(result, "payload"),
              "fs read");
        check(call(vm, module, "write_case", &result, &error) &&
                  fs.files["output.txt"] == "written",
              "fs write");
        check(call(vm, module, "ensure_case", &result, &error) && fs.directories.contains("cache"),
              "fs ensure directory");
        check(call(vm, module, "read_or_case", &result, &error) &&
                  equals_string(result, "fallback"),
              "fs read default");
        check(call(vm, module, "copy_case", &result, &error) &&
                  fs.files["copy.txt"] == "payload",
              "fs copy");
        check(call(vm, module, "delete_case", &result, &error) &&
                  !fs.files.contains("input.txt"),
              "fs delete");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated fs tests passed\n";
    return EXIT_SUCCESS;
}
