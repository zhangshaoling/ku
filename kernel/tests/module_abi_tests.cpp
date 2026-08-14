#include "dao/dao.h"
#include "dao/format.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

#define CHECK(name, condition)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            std::cerr << "FAIL: " << name << '\n';                                               \
            ++failures;                                                                           \
        }                                                                                         \
    } while (false)

uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

dao_bytes view(std::string_view value) {
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

size_t section_offset(const std::vector<uint8_t>& bytes, dao::SectionType type) {
    const uint32_t count = read_u32(bytes, 12);
    for (uint32_t index = 0; index < count; ++index) {
        const size_t entry = dao::kHeaderSize + index * dao::kSectionEntrySize;
        if (read_u32(bytes, entry) == static_cast<uint32_t>(type))
            return read_u32(bytes, entry + 4);
    }
    return bytes.size();
}

dao::FunctionSpec constant_function(int64_t value) {
    dao::FunctionSpec function;
    function.register_count = 1;
    function.code = {{dao::Opcode::LoadI64, 0, 0, 0, 0, value},
                     {dao::Opcode::Return, 0, 0, 0, 0, 0}};
    return function;
}

std::vector<uint8_t> provider_bytes(std::string_view identity, dao::SemanticVersion version,
                                    int64_t multiplier) {
    dao::ModuleBuilder builder;
    builder.set_identity(identity, version);
    dao::FunctionSpec function;
    function.parameter_count = 1;
    function.register_count = 3;
    function.code = {{dao::Opcode::LoadI64, 0, 1, 0, 0, multiplier},
                     {dao::Opcode::MulI64, 0, 2, 0, 1, 0},
                     {dao::Opcode::Return, 0, 0, 2, 0, 0}};
    const uint32_t index = builder.add_function(std::move(function));
    builder.add_export(symbol("double"), index);
    return builder.encode();
}

std::vector<uint8_t> consumer_bytes(std::string_view identity,
                                    std::string_view dependency,
                                    dao::SemanticVersion dependency_version,
                                    uint32_t imported_symbol = symbol("double"),
                                    uint16_t imported_arity = 1) {
    dao::ModuleBuilder builder;
    builder.set_identity(identity, {1, 0, 0});
    const uint32_t import = builder.add_module_import(
        dependency, dependency_version, imported_symbol, imported_arity);
    dao::FunctionSpec function;
    function.register_count = static_cast<uint16_t>(imported_arity + 2);
    function.code.push_back({dao::Opcode::LoadI64, 0, 0, 0, 0, 21});
    if (imported_arity == 2)
        function.code.push_back({dao::Opcode::LoadI64, 0, 1, 0, 0, 21});
    function.code.push_back(
        {dao::Opcode::CallModule, 0, static_cast<uint16_t>(imported_arity), 0,
         imported_arity, import});
    function.code.push_back(
        {dao::Opcode::Return, 0, 0, static_cast<uint16_t>(imported_arity), 0, 0});
    const uint32_t index = builder.add_function(std::move(function));
    builder.add_export(symbol("main"), index);
    return builder.encode();
}

std::vector<uint8_t> graph_module(std::string_view identity, uint32_t own_symbol,
                                  std::string_view dependency, uint32_t dependency_symbol) {
    dao::ModuleBuilder builder;
    builder.set_identity(identity, {1, 0, 0});
    builder.add_module_import(dependency, {1, 0, 0}, dependency_symbol, 0);
    const uint32_t index = builder.add_function(constant_function(1));
    builder.add_export(own_symbol, index);
    return builder.encode();
}

dao_module* load(dao_vm* vm, const std::vector<uint8_t>& bytes, dao_error* error) {
    dao_module* module = nullptr;
    if (dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, error) != DAO_OK)
        return nullptr;
    return module;
}

} // namespace

int main(int argc, char** argv) {
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    CHECK("create VM", vm != nullptr);
    if (vm == nullptr)
        return EXIT_FAILURE;

    dao_error error{};
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<uint8_t> file_bytes((std::istreambuf_iterator<char>(input)), {});
        dao_module* file_module = load(vm, file_bytes, &error);
        dao_module_identity file_identity{};
        file_identity.struct_size = sizeof(file_identity);
        CHECK("load identified CLI module", file_module != nullptr);
        CHECK("CLI module identity",
              file_module != nullptr &&
                  dao_module_get_identity(file_module, &file_identity) == DAO_OK &&
                  std::string_view(
                      reinterpret_cast<const char*>(file_identity.name.data),
                      file_identity.name.size) == "ku:std/core" &&
                  file_identity.version_major == 1 && file_identity.version_minor == 0 &&
                  file_identity.version_patch == 0);
        dao_module_release(file_module);
    }
    const auto provider_data = provider_bytes("ku:std/math", {1, 0, 0}, 2);
    const auto consumer_data =
        consumer_bytes("ku:app/main", "ku:std/math", {1, 0, 0});
    dao_module* provider = load(vm, provider_data, &error);
    dao_module* consumer = load(vm, consumer_data, &error);
    CHECK("load identified provider", provider != nullptr);
    CHECK("load identified consumer", consumer != nullptr);

    auto bad_metadata = provider_data;
    const size_t metadata_offset = section_offset(bad_metadata, dao::SectionType::Metadata);
    bad_metadata[metadata_offset + 16] = 1;
    dao_module* rejected = nullptr;
    CHECK("reject nonzero module metadata flags",
          dao_vm_load_module(vm, {bad_metadata.data(), bad_metadata.size()}, &rejected, &error) ==
              DAO_VERIFY_ERROR);

    auto cross_paired_version = provider_data;
    cross_paired_version[6] = static_cast<uint8_t>(dao::kLegacyVmAbiVersion);
    cross_paired_version[7] = 0;
    CHECK("reject cross-paired format and VM ABI",
          dao_vm_load_module(vm,
                             {cross_paired_version.data(), cross_paired_version.size()},
                             &rejected, &error) == DAO_BAD_MODULE);

    dao::ModuleBuilder invalid_call_builder;
    invalid_call_builder.set_identity("ku:bad/call", {1, 0, 0});
    dao::FunctionSpec invalid_call;
    invalid_call.register_count = 1;
    invalid_call.code = {{dao::Opcode::CallModule, 0, 0, 0, 0, 0},
                         {dao::Opcode::Return, 0, 0, 0, 0, 0}};
    const uint32_t invalid_call_index =
        invalid_call_builder.add_function(std::move(invalid_call));
    invalid_call_builder.add_export(1, invalid_call_index);
    const auto invalid_call_data = invalid_call_builder.encode();
    CHECK("reject CALL_MODULE without import record",
          dao_vm_load_module(vm, {invalid_call_data.data(), invalid_call_data.size()}, &rejected,
                             &error) == DAO_VERIFY_ERROR);

    dao_module_identity identity{};
    identity.struct_size = sizeof(identity);
    CHECK("read module identity",
          dao_module_get_identity(provider, &identity) == DAO_OK &&
              std::string_view(reinterpret_cast<const char*>(identity.name.data),
                               identity.name.size) == "ku:std/math" &&
              identity.version_major == 1 && identity.version_minor == 0 &&
              identity.version_patch == 0);

    CHECK("link unresolved consumer", dao_vm_link_module(vm, consumer, &error) == DAO_OK);
    dao_function main_function = 0;
    CHECK("find consumer export",
          dao_module_find_export(consumer, symbol("main"), &main_function) == DAO_OK);
    dao_value result{};
    CHECK("unresolved module call",
          dao_vm_call(vm, consumer, main_function, nullptr, 0, &result, &error) ==
              DAO_IMPORT_NOT_FOUND);
    CHECK("link provider", dao_vm_link_module(vm, provider, &error) == DAO_OK);
    CHECK("execute linked module call",
          dao_vm_call(vm, consumer, main_function, nullptr, 0, &result, &error) == DAO_OK &&
              result.type == DAO_VALUE_I64 && result.payload == 42);

    dao_module* found = nullptr;
    CHECK("find linked module",
          dao_vm_find_module(vm, view("ku:std/math"), 1, 0, 0, &found) == DAO_OK &&
              found == provider);
    dao_module_release(found);

    dao_module* cached_provider = load(vm, provider_data, &error);
    CHECK("cache keeps identified bytes canonical", cached_provider == provider);
    dao_module_release(cached_provider);
    CHECK("idempotent link", dao_vm_link_module(vm, provider, &error) == DAO_OK);

    const auto conflict_data = provider_bytes("ku:std/math", {1, 0, 0}, 3);
    dao_module* conflict = load(vm, conflict_data, &error);
    CHECK("load conflicting module", conflict != nullptr);
    CHECK("reject identity conflict",
          dao_vm_link_module(vm, conflict, &error) == DAO_MODULE_CONFLICT);

    const auto missing_export_data =
        consumer_bytes("ku:app/missing", "ku:std/math", {1, 0, 0}, symbol("missing"));
    dao_module* missing_export = load(vm, missing_export_data, &error);
    CHECK("load missing-export consumer", missing_export != nullptr);
    CHECK("reject missing module export",
          dao_vm_link_module(vm, missing_export, &error) == DAO_EXPORT_NOT_FOUND);

    const auto wrong_arity_data =
        consumer_bytes("ku:app/wrong-arity", "ku:std/math", {1, 0, 0}, symbol("double"), 2);
    dao_module* wrong_arity = load(vm, wrong_arity_data, &error);
    CHECK("load wrong-arity consumer", wrong_arity != nullptr);
    CHECK("reject module signature mismatch",
          dao_vm_link_module(vm, wrong_arity, &error) == DAO_MODULE_VERSION_MISMATCH);

    const auto cycle_a_data = graph_module("ku:cycle/a", 100, "ku:cycle/b", 200);
    const auto cycle_b_data = graph_module("ku:cycle/b", 200, "ku:cycle/a", 100);
    dao_module* cycle_a = load(vm, cycle_a_data, &error);
    dao_module* cycle_b = load(vm, cycle_b_data, &error);
    CHECK("link first half of cycle", dao_vm_link_module(vm, cycle_a, &error) == DAO_OK);
    CHECK("reject completed module cycle",
          dao_vm_link_module(vm, cycle_b, &error) == DAO_MODULE_CYCLE);

    dao::ModuleBuilder callable_builder;
    callable_builder.set_identity("ku:boundary/provider", {1, 0, 0});
    const uint32_t helper_index = callable_builder.add_function(constant_function(42));
    dao::FunctionSpec callable_export;
    callable_export.register_count = 1;
    callable_export.code = {{dao::Opcode::LoadFunction, 0, 0, 0, 0, helper_index},
                            {dao::Opcode::Return, 0, 0, 0, 0, 0}};
    const uint32_t callable_index =
        callable_builder.add_function(std::move(callable_export));
    callable_builder.add_export(symbol("callable"), callable_index);
    const auto callable_data = callable_builder.encode();
    const auto callable_consumer_data =
        consumer_bytes("ku:boundary/consumer", "ku:boundary/provider", {1, 0, 0},
                       symbol("callable"), 0);
    dao_module* callable = load(vm, callable_data, &error);
    dao_module* callable_consumer = load(vm, callable_consumer_data, &error);
    CHECK("link callable provider", dao_vm_link_module(vm, callable, &error) == DAO_OK);
    CHECK("link callable consumer",
          dao_vm_link_module(vm, callable_consumer, &error) == DAO_OK);
    dao_function callable_main = 0;
    CHECK("find callable consumer export",
          dao_module_find_export(callable_consumer, symbol("main"), &callable_main) == DAO_OK);
    CHECK("reject module-local function result",
          dao_vm_call(vm, callable_consumer, callable_main, nullptr, 0, &result, &error) ==
              DAO_TYPE_ERROR);

    dao::ModuleBuilder legacy_builder;
    const uint32_t legacy_index = legacy_builder.add_function(constant_function(42));
    legacy_builder.add_export(symbol("legacy"), legacy_index);
    const auto legacy_data = legacy_builder.encode();
    dao_module* legacy = load(vm, legacy_data, &error);
    CHECK("legacy v1 remains loadable", legacy != nullptr);
    dao_module_identity legacy_identity{};
    legacy_identity.struct_size = sizeof(legacy_identity);
    CHECK("legacy module has no logical identity",
          dao_module_get_identity(legacy, &legacy_identity) == DAO_MODULE_IDENTITY_MISSING);
    CHECK("legacy module cannot be linked",
          dao_vm_link_module(vm, legacy, &error) == DAO_MODULE_IDENTITY_MISSING);

    dao_module_release(legacy);
    dao_module_release(callable_consumer);
    dao_module_release(callable);
    dao_module_release(cycle_b);
    dao_module_release(cycle_a);
    dao_module_release(wrong_arity);
    dao_module_release(missing_export);
    dao_module_release(conflict);
    dao_module_release(consumer);
    dao_module_release(provider);
    dao_vm_destroy(vm);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
