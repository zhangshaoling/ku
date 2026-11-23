#include "dao/module_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace {
int failures = 0;
bool check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    return ok;
}
struct TemporaryStore {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("ku-record-store-native-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryStore() { std::error_code error; std::filesystem::remove_all(root, error); }
};
using Vm = std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)>;
using Module = std::unique_ptr<dao_module, decltype(&dao_module_release)>;
dao::Instruction op(dao::Opcode opcode, uint16_t dst = 0, uint16_t a = 0, uint16_t b = 0, int64_t immediate = 0) {
    return {opcode, 0, dst, a, b, immediate};
}

std::vector<uint8_t> provider() {
    dao::ModuleBuilder builder;
    builder.set_identity("ku:test/record-provider", {1, 0, 0});
    const auto type = builder.add_record_type("Point");
    builder.add_record_field(type, "x", DAO_VALUE_I64);
    dao::FunctionSpec scalar;
    scalar.register_count = 3;
    scalar.code = {op(dao::Opcode::LoadI64, 0, 0, 0, 42), op(dao::Opcode::MakeRecord, 1, 0, 1, type),
                   op(dao::Opcode::RecordGet, 2, 1, 0, type), op(dao::Opcode::Return, 0, 2)};
    builder.add_export(1, builder.add_function(std::move(scalar)));
    dao::FunctionSpec record;
    record.register_count = 2;
    record.code = {op(dao::Opcode::LoadI64, 0, 0, 0, 42), op(dao::Opcode::MakeRecord, 1, 0, 1, type),
                   op(dao::Opcode::Return, 0, 1)};
    builder.add_export(2, builder.add_function(std::move(record)));
    return builder.encode();
}

void binary_policy(const std::filesystem::path& root) {
    dao::store::ModuleStore enabled(root, DAO_FEATURE_RECORDS), disabled(root);
    dao_error error{};
    const auto bytes = provider();
    check(disabled.save(bytes, {}, &error) == DAO_UNSUPPORTED_FEATURE && !std::filesystem::exists(root),
          "default save rejects before filesystem side effects");
    if (!check(enabled.save(bytes, {}, &error) == DAO_OK, "enabled save")) return;
    check(enabled.save(bytes, {}, &error) == DAO_OK, "idempotent enabled save");
    std::vector<uint8_t> loaded;
    if (!check(enabled.load("ku:test/record-provider", {1, 0, 0}, &loaded, &error) == DAO_OK && loaded == bytes,
               "enabled load preserves exact bytes")) return;
    check(disabled.load("ku:test/record-provider", {1, 0, 0}, &loaded, &error) == DAO_UNSUPPORTED_FEATURE && loaded.empty(),
          "disabled load rejects after enabled load and clears output");
    check(disabled.verify("ku:test/record-provider", {1, 0, 0}, &error) == DAO_UNSUPPORTED_FEATURE,
          "verify uses same policy");
    const dao_vm_config config = dao_vm_config_default();
    Vm owner(dao_vm_create(&config), dao_vm_destroy), other(dao_vm_create(&config), dao_vm_destroy);
    if (!owner || !other) { check(false, "create VMs"); return; }
    check(dao_vm_set_features(owner.get(), DAO_FEATURE_RECORDS) == DAO_OK, "explicit execution policy");
    dao_module* raw = nullptr;
    if (!check(dao_vm_load_module(owner.get(), {bytes.data(), bytes.size()}, &raw, &error) == DAO_OK, "load stored provider")) return;
    Module module(raw, dao_module_release);
    dao_module* cached_raw = nullptr;
    check(dao_vm_load_module(owner.get(), {bytes.data(), bytes.size()}, &cached_raw, &error) == DAO_OK && cached_raw == raw,
          "enabled VM cache reuses stored provider");
    Module cached(cached_raw, dao_module_release);
    dao_module* rejected = nullptr;
    check(dao_vm_load_module(other.get(), {bytes.data(), bytes.size()}, &rejected, &error) == DAO_UNSUPPORTED_FEATURE && !rejected,
          "stored bytes do not authorize another VM");
    check(dao_vm_link_module(other.get(), module.get(), &error) == DAO_UNSUPPORTED_FEATURE,
          "cached module cannot bypass target VM policy");
    dao_value result{}, field{};
    check(dao_vm_call(owner.get(), module.get(), 1, nullptr, 0, &result, &error) == DAO_OK &&
          result.type == DAO_VALUE_RECORD && dao_value_record_get(owner.get(), &result, 0, &field) == DAO_OK &&
          field.type == DAO_VALUE_I64 && field.payload == 42, "stored module executes Record and read-only query");

    dao::ModuleBuilder builder;
    builder.set_identity("ku:test/record-consumer", {1, 0, 0});
    for (uint32_t symbol = 1; symbol <= 2; ++symbol) {
        const auto import = builder.add_module_import("ku:test/record-provider", {1, 0, 0}, symbol, 0);
        dao::FunctionSpec fn;
        fn.register_count = 1;
        fn.code = {op(dao::Opcode::CallModule, 0, 0, 0, import), op(dao::Opcode::Return, 0, 0)};
        builder.add_export(symbol, builder.add_function(std::move(fn)));
    }
    const auto consumer_bytes = builder.encode();
    check(disabled.save(consumer_bytes, {}, &error) == DAO_OK, "legacy consumer saves without Record policy");
    check(disabled.load("ku:test/record-consumer", {1, 0, 0}, &loaded, &error) == DAO_OK && loaded == consumer_bytes,
          "store does not link or recursively load runtime dependencies");
    raw = nullptr;
    if (!check(dao_vm_load_module(owner.get(), {loaded.data(), loaded.size()}, &raw, &error) == DAO_OK, "load consumer")) return;
    Module consumer(raw, dao_module_release);
    check(dao_vm_link_module(owner.get(), module.get(), &error) == DAO_OK, "explicit provider link");
    check(dao_vm_call(owner.get(), consumer.get(), 0, nullptr, 0, &result, &error) == DAO_OK &&
          result.type == DAO_VALUE_I64 && result.payload == 42, "safe scalar crosses stored module boundary");
    check(dao_vm_call(owner.get(), consumer.get(), 1, nullptr, 0, &result, &error) == DAO_TYPE_ERROR,
          "Record still rejected across stored module boundary");
    auto malformed = bytes;
    malformed[12] = 0;
    check(enabled.save(malformed, {}, &error) != DAO_OK, "opt-in never bypasses verifier");
}

void source_policy(const std::filesystem::path& root, const std::string& compiler) {
    dao::store::ModuleStore enabled(root, DAO_FEATURE_RECORDS), disabled(root);
    const std::string source = "型 点 { 横: 整数 } 思 main() { 值 = 点 { 横: 42 }; 返 值.横 }";
    dao_error error{};
    check(disabled.save_source(compiler, source, "ku:test/source", {1, 0, 0}, &error) == DAO_UNSUPPORTED_FEATURE,
          "source save defaults to no Records");
    if (!check(enabled.save_source(compiler, source, "ku:test/source", {1, 0, 0}, &error) == DAO_OK,
               "enabled source save")) return;
    check(enabled.save_derived_source(compiler, source, "ku:test/source", {2, 0, 0},
          "ku:test/source", {1, 0, 0}, &error) == DAO_OK, "enabled derived Record source save");
    check(disabled.save_derived_source(compiler, "思 main() { 返 43 }", "ku:test/child", {1, 0, 0},
          "ku:test/source", {1, 0, 0}, &error) == DAO_UNSUPPORTED_FEATURE, "parent verification shares policy");
    std::vector<uint8_t> output{1, 2, 3};
    check(enabled.load("ku:test/child", {1, 0, 0}, &output, &error) == DAO_IMPORT_NOT_FOUND && output.empty(),
          "rejected derivation does not publish child");
    try {
        dao::store::ModuleStore invalid(root / "invalid", DAO_FEATURE_RECORDS | 2);
        check(false, "unknown feature bits rejected");
    } catch (const std::invalid_argument&) {
        check(!std::filesystem::exists(root / "invalid"), "invalid policy has no filesystem side effects");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    const std::string compiler((std::istreambuf_iterator<char>(input)), {});
    if (compiler.empty()) return EXIT_FAILURE;
    TemporaryStore temporary;
    binary_policy(temporary.root / "binary");
    source_policy(temporary.root / "source", compiler);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
