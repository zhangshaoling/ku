#include <dao/assemble.hpp>
#include <dao/disassemble.hpp>
#include <dao/module_store.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    dao::ModuleBuilder builder;
    builder.set_identity("ku:test/installed-records", {1, 0, 0});
    const auto type = builder.add_record_type("Point");
    builder.add_record_field(type, "x", DAO_VALUE_I64);
    dao::FunctionSpec fn;
    fn.register_count = 3;
    fn.code = {{dao::Opcode::LoadI64, 0, 0, 0, 0, 42},
               {dao::Opcode::MakeRecord, 0, 1, 0, 1, type},
               {dao::Opcode::RecordGet, 0, 2, 1, 0, type},
               {dao::Opcode::Return, 0, 0, 2, 0, 0}};
    builder.add_export(1, builder.add_function(std::move(fn)));
    const auto expected = builder.encode();
    dao::DisassembledModule model;
    dao_error error{};
    std::vector<uint8_t> actual;
    if (dao::disassemble({expected.data(), expected.size()}, &model, &error) != DAO_OK ||
        !dao::assemble_text(dao::to_text(model), &actual, &error) || actual != expected) {
        std::cerr << error.message << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "installed Record assembly round trip passed\n";
    const auto root = std::filesystem::temp_directory_path() /
        ("ku-sdk-record-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    dao::store::ModuleStore disabled(root), enabled(root, DAO_FEATURE_RECORDS);
    std::vector<uint8_t> loaded;
    const bool stored = disabled.save(actual, {}, &error) == DAO_UNSUPPORTED_FEATURE &&
        !std::filesystem::exists(root) && enabled.save(actual, {}, &error) == DAO_OK &&
        enabled.load("ku:test/installed-records", {1, 0, 0}, &loaded, &error) == DAO_OK && loaded == actual &&
        disabled.load("ku:test/installed-records", {1, 0, 0}, &loaded, &error) == DAO_UNSUPPORTED_FEATURE && loaded.empty();
    std::filesystem::remove_all(root);
    if (!stored) { std::cerr << error.message << '\n'; return EXIT_FAILURE; }
    std::cout << "installed Record store policy passed\n";
    return EXIT_SUCCESS;
}
