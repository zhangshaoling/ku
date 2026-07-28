#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <deque>
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
    std::deque<std::string> strings;
};

struct Sources {
    std::string fs;
    std::string string;
    std::string io;
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

dao_status string_length(void*, const dao_value* args, size_t count, dao_value* out) {
    dao_bytes bytes{};
    if (count != 1 || dao_value_get_view(&args[0], &bytes) != DAO_OK) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_I64, 0, static_cast<int64_t>(bytes.size)};
    return DAO_OK;
}

dao_status string_concat(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    std::string left;
    std::string right;
    if (count != 2 || !string_arg(args[0], &left) || !string_arg(args[1], &right))
        return DAO_TYPE_ERROR;
    auto* fs = static_cast<HostFs*>(user_data);
    fs->strings.push_back(left + right);
    const auto& result = fs->strings.back();
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(result.data()), result.size()}, out);
}

bool resolve_standard(void* user_data, std::string_view path, std::string* source,
                      std::string* error) {
    const auto* sources = static_cast<Sources*>(user_data);
    if (path == "std/fs" || path == "fs") *source = sources->fs;
    else if (path == "std/string" || path == "string") *source = sources->string;
    else if (path == "std/io") *source = sources->io;
    else {
        *error = "unknown module";
        return false;
    }
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
    if (argc != 4) return EXIT_FAILURE;
    Sources sources{};
    std::string* destinations[] = {&sources.fs, &sources.string, &sources.io};
    for (int index = 0; index < 3; ++index) {
        std::ifstream input(argv[index + 1], std::ios::binary);
        destinations[index]->assign(std::istreambuf_iterator<char>(input), {});
        if (!input && destinations[index]->empty()) return EXIT_FAILURE;
    }

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
    options.import_resolver = resolve_standard;
    options.import_user_data = &sources;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated fs");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated fs");

    HostFs fs{{{"input.txt", "payload"}}, {}, {}};
    const dao_host_function hosts[] = {
        {sizeof(dao_host_function), symbol_id("path_exists"), 1, 0, path_exists, &fs},
        {sizeof(dao_host_function), symbol_id("read_file"), 1, 0, read_file, &fs},
        {sizeof(dao_host_function), symbol_id("write_file"), 2, 0, write_file, &fs},
        {sizeof(dao_host_function), symbol_id("mkdir"), 1, 0, make_directory, &fs},
        {sizeof(dao_host_function), symbol_id("delete_file"), 1, 0, delete_file, &fs},
        {sizeof(dao_host_function), symbol_id("host_string_length"), 1, 0, string_length, &fs},
        {sizeof(dao_host_function), symbol_id("host_string_concat"), 2, 0, string_concat, &fs},
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

    const char* io_program = R"(
import "std/io" as io
thought io_exists_case() { io_file_exists("journal.txt") }
thought io_size_case() { io_file_size("journal.txt") }
thought io_append_case() { io_append_file("journal.txt", "-tail") }
thought io_append_line_case() { io_append_line("journal.txt", "next") }
thought io_read_or_case() { io_read_or("absent.txt", "fallback") }
thought io_copy_case() { io_copy_file("journal.txt", "journal-copy.txt") }
thought io_delete_case() { io_safe_delete("journal-copy.txt") }
thought passthrough(value) { value }
thought io_callable_default_case() { io_read_or("absent-callable.txt", passthrough) }
    )";
    fs.files["journal.txt"] = "base";
    dao::ModuleBuilder io_builder;
    io_builder.set_identity("ku:test/io-suite", {1, 0, 0});
    check(dao::km::compile(io_program, io_builder, &error, options), "compile migrated io");
    const auto io_bytes = io_builder.encode();
    dao::ModuleBuilder fs_provider_builder;
    fs_provider_builder.set_identity("ku:std/fs", {1, 0, 0});
    check(dao::km::compile(sources.fs, fs_provider_builder, &error),
          "compile identified fs provider");
    const auto fs_provider_bytes = fs_provider_builder.encode();
    dao::ModuleBuilder string_provider_builder;
    string_provider_builder.set_identity("ku:std/string", {1, 0, 0});
    check(dao::km::compile(sources.string, string_provider_builder, &error),
          "compile identified string provider");
    const auto string_provider_bytes = string_provider_builder.encode();
    dao_module* io_module = nullptr;
    dao_module* fs_provider = nullptr;
    dao_module* string_provider = nullptr;
    check(dao_vm_load_module(vm, {io_bytes.data(), io_bytes.size()}, &io_module, &error) == DAO_OK,
          "load migrated io");
    check(dao_vm_load_module(vm, {fs_provider_bytes.data(), fs_provider_bytes.size()},
                             &fs_provider, &error) == DAO_OK,
          "load identified fs provider");
    check(dao_vm_load_module(vm, {string_provider_bytes.data(), string_provider_bytes.size()},
                             &string_provider, &error) == DAO_OK,
          "load identified string provider");
    check(io_module != nullptr && dao_vm_link_module(vm, io_module, &error) == DAO_OK,
          "link io before providers");
    if (io_module != nullptr && fs_provider != nullptr && string_provider != nullptr) {
        dao_value result{};
        dao_function exists_function{};
        dao_function size_function{};
        check(dao_module_find_export(io_module, symbol_id("io_exists_case"), &exists_function) ==
                      DAO_OK &&
                  dao_vm_call(vm, io_module, exists_function, nullptr, 0, &result, &error) ==
                      DAO_IMPORT_NOT_FOUND,
              "io fs dependency unresolved");
        check(dao_vm_link_module(vm, fs_provider, &error) == DAO_OK,
              "link identified fs provider");
        check(dao_vm_call(vm, io_module, exists_function, nullptr, 0, &result, &error) == DAO_OK &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "io reaches linked fs provider");
        check(dao_module_find_export(io_module, symbol_id("io_size_case"), &size_function) ==
                      DAO_OK &&
                  dao_vm_call(vm, io_module, size_function, nullptr, 0, &result, &error) ==
                      DAO_IMPORT_NOT_FOUND,
              "io string dependency unresolved");
        check(dao_vm_link_module(vm, string_provider, &error) == DAO_OK,
              "link identified string provider");
        dao_function callable_default{};
        check(dao_module_find_export(io_module, symbol_id("io_callable_default_case"),
                                     &callable_default) == DAO_OK &&
                  dao_vm_call(vm, io_module, callable_default, nullptr, 0, &result, &error) ==
                      DAO_TYPE_ERROR,
              "io rejects callable default across module boundary");
        check(call(vm, io_module, "io_exists_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "io file exists");
        check(call(vm, io_module, "io_size_case", &result, &error) &&
                  result.type == DAO_VALUE_I64 && result.payload == 4,
              "io file size");
        check(call(vm, io_module, "io_append_case", &result, &error) &&
                  fs.files["journal.txt"] == "base-tail",
              "io append file");
        check(call(vm, io_module, "io_append_line_case", &result, &error) &&
                  fs.files["journal.txt"] == "base-tail\nnext",
              "io append line");
        check(call(vm, io_module, "io_read_or_case", &result, &error) &&
                  equals_string(result, "fallback"),
              "io read default");
        check(call(vm, io_module, "io_copy_case", &result, &error) &&
                  fs.files["journal-copy.txt"] == "base-tail\nnext",
              "io copy file");
        check(call(vm, io_module, "io_delete_case", &result, &error) &&
                  !fs.files.contains("journal-copy.txt"),
              "io safe delete");
    }
    if (io_module != nullptr) dao_module_release(io_module);
    if (fs_provider != nullptr) dao_module_release(fs_provider);
    if (string_provider != nullptr) dao_module_release(string_provider);
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated fs tests passed\n";
    return EXIT_SUCCESS;
}
