#include "dao/module_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) { hash ^= byte; hash *= 16777619u; }
    return hash;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ku_module_store_test";
    std::filesystem::remove_all(root);
    dao::ModuleBuilder builder;
    builder.set_identity("ku:memory/answer", {1, 0, 0});
    dao::FunctionSpec function;
    function.register_count = 1;
    function.code = {{dao::Opcode::LoadI64, 0, 0, 0, 0, 42},
                     {dao::Opcode::Return, 0, 0, 0, 0, 0}};
    const auto index = builder.add_function(std::move(function));
    builder.add_export(symbol("main"), index);
    const auto bytes = builder.encode();
    dao::store::ModuleStore store(root);
    dao_error error{};
    const auto compiler_path = std::filesystem::path(__FILE__).parent_path().parent_path() / "selfhost" / "compiler.ku";
    std::ifstream compiler_file(compiler_path, std::ios::binary);
    const std::string compiler_source((std::istreambuf_iterator<char>(compiler_file)), {});
    if (compiler_source.empty()) return EXIT_FAILURE;
    if (store.save_source(compiler_source, "思 main() { 返 42 }", "ku:memory/source", {1, 0, 0}, &error) != DAO_OK)
        return EXIT_FAILURE;
    if (store.save_derived_source(compiler_source, "思 main() { 返 43 }", "ku:memory/source", {2, 0, 0},
                                  "ku:memory/source", {1, 0, 0}, &error) != DAO_OK)
        return EXIT_FAILURE;
    if (store.verify("ku:memory/source", {2, 0, 0}, &error) != DAO_OK) return EXIT_FAILURE;
    if (store.save(bytes, "思 main() { 返 42 }", &error) != DAO_OK) return EXIT_FAILURE;
    std::vector<uint8_t> loaded;
    if (store.load("ku:memory/answer", {1, 0, 0}, &loaded, &error) != DAO_OK || loaded != bytes)
        return EXIT_FAILURE;
    if (store.load("ku:memory/missing", {1, 0, 0}, &loaded, &error) != DAO_IMPORT_NOT_FOUND)
        return EXIT_FAILURE;
    if (store.load("ku:memory/answer", {1, 0, 0}, &loaded, &error) != DAO_OK || loaded != bytes)
        return EXIT_FAILURE;
    bool found_manifest = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        found_manifest = found_manifest || entry.path().extension() == ".json";
    if (!found_manifest) return EXIT_FAILURE;
    if (store.verify("ku:memory/answer", {1, 0, 0}, &error) != DAO_OK) return EXIT_FAILURE;
    bool tested_tampering = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() == ".dao") {
            std::ifstream candidate(entry.path(), std::ios::binary);
            const std::vector<uint8_t> candidate_bytes((std::istreambuf_iterator<char>(candidate)), {});
            candidate.close();
            if (candidate_bytes != bytes) continue;
            tested_tampering = true;
            std::fstream tampered(entry.path(), std::ios::in | std::ios::out | std::ios::binary);
            tampered.seekp(0, std::ios::end);
            const auto position = tampered.tellp();
            tampered.seekp(position - std::streamoff(1));
            const char byte = static_cast<char>(bytes.back() ^ 1);
            tampered.write(&byte, 1);
            tampered.close();
            if (store.verify("ku:memory/answer", {1, 0, 0}, &error) == DAO_OK) return EXIT_FAILURE;
            std::ofstream restore(entry.path(), std::ios::binary | std::ios::trunc);
            restore.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            break;
        }
    }
    if (!tested_tampering) return EXIT_FAILURE;
    if (store.save(bytes, {}, &error) != DAO_OK) return EXIT_FAILURE;
    std::filesystem::path answer_path;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".dao") continue;
        std::ifstream candidate(entry.path(), std::ios::binary);
        const std::vector<uint8_t> candidate_bytes((std::istreambuf_iterator<char>(candidate)), {});
        if (candidate_bytes == bytes) answer_path = entry.path();
    }
    if (answer_path.empty()) return EXIT_FAILURE;
    auto answer_manifest = answer_path;
    answer_manifest.replace_extension(".json");
    // A retry must repair an interrupted save that published bytes but no manifest.
    std::filesystem::remove(answer_manifest);
    if (store.verify("ku:memory/answer", {1, 0, 0}, &error) == DAO_OK) return EXIT_FAILURE;
    if (store.save(bytes, {}, &error) != DAO_OK ||
        store.verify("ku:memory/answer", {1, 0, 0}, &error) != DAO_OK) {
        std::cerr << "save retry did not recover the missing manifest\n";
        return EXIT_FAILURE;
    }
    std::ifstream manifest_input(answer_manifest, std::ios::binary);
    const std::string manifest_text((std::istreambuf_iterator<char>(manifest_input)), {});
    manifest_input.close();
    {
        std::ofstream invalid_manifest(answer_manifest, std::ios::binary | std::ios::trunc);
        invalid_manifest << "{}";
    }
    // An existing mismatched manifest must be rejected, not silently overwritten.
    if (store.save(bytes, {}, &error) != DAO_BAD_MODULE) return EXIT_FAILURE;
    {
        std::ofstream restore_manifest(answer_manifest, std::ios::binary | std::ios::trunc);
        restore_manifest << manifest_text;
    }
    if (store.save(bytes, {}, &error) != DAO_OK) return EXIT_FAILURE;
    if (store.load("ku:memory/answer", {1, 0, 0}, &loaded, &error) != DAO_OK || loaded != bytes)
        return EXIT_FAILURE;
    auto conflicting = bytes;
    conflicting.back() ^= 1;
    dao::ModuleBuilder alternate_builder;
    alternate_builder.set_identity("ku:memory/answer", {1, 0, 0});
    dao::FunctionSpec alternate_function;
    alternate_function.register_count = 1;
    alternate_function.code = {{dao::Opcode::LoadI64, 0, 0, 0, 0, 43},
                               {dao::Opcode::Return, 0, 0, 0, 0, 0}};
    const auto alternate_index = alternate_builder.add_function(std::move(alternate_function));
    alternate_builder.add_export(symbol("main"), alternate_index);
    conflicting = alternate_builder.encode();
    if (store.save(conflicting, {}, &error) != DAO_MODULE_CONFLICT) return EXIT_FAILURE;
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr) return EXIT_FAILURE;
    dao_module* module = nullptr;
    if (dao_vm_load_module(vm, {loaded.data(), loaded.size()}, &module, &error) != DAO_OK) return EXIT_FAILURE;
    dao_function main_function = 0;
    if (dao_module_find_export(module, symbol("main"), &main_function) != DAO_OK) return EXIT_FAILURE;
    dao_value result{};
    if (dao_vm_call(vm, module, main_function, nullptr, 0, &result, &error) != DAO_OK ||
        result.type != DAO_VALUE_I64 || result.payload != 42) return EXIT_FAILURE;
    dao_module_release(module);
    dao::ModuleBuilder provider_builder;
    provider_builder.set_identity("ku:memory/provider", {1, 0, 0});
    dao::FunctionSpec provider_function;
    provider_function.parameter_count = 1;
    provider_function.register_count = 3;
    provider_function.code = {{dao::Opcode::LoadI64, 0, 1, 0, 0, 2},
                              {dao::Opcode::MulI64, 0, 2, 0, 1, 0},
                              {dao::Opcode::Return, 0, 0, 2, 0, 0}};
    const auto provider_index = provider_builder.add_function(std::move(provider_function));
    provider_builder.add_export(symbol("double"), provider_index);
    const auto provider_bytes = provider_builder.encode();
    if (store.save(provider_bytes, {}, &error) != DAO_OK) return EXIT_FAILURE;
    dao::ModuleBuilder consumer_builder;
    consumer_builder.set_identity("ku:memory/consumer", {1, 0, 0});
    const auto import_index = consumer_builder.add_module_import(
        "ku:memory/provider", {1, 0, 0}, symbol("double"), 1);
    dao::FunctionSpec consumer_function;
    consumer_function.register_count = 3;
    consumer_function.code = {{dao::Opcode::LoadI64, 0, 0, 0, 0, 21},
                              {dao::Opcode::CallModule, 0, 1, 0, 1, import_index},
                              {dao::Opcode::Return, 0, 0, 1, 0, 0}};
    const auto consumer_index = consumer_builder.add_function(std::move(consumer_function));
    consumer_builder.add_export(symbol("main"), consumer_index);
    const auto consumer_bytes = consumer_builder.encode();
    if (store.save(consumer_bytes, {}, &error) != DAO_OK) return EXIT_FAILURE;
    std::vector<uint8_t> loaded_provider, loaded_consumer;
    if (store.load("ku:memory/provider", {1, 0, 0}, &loaded_provider, &error) != DAO_OK ||
        store.load("ku:memory/consumer", {1, 0, 0}, &loaded_consumer, &error) != DAO_OK) return EXIT_FAILURE;
    dao_module* provider_module = nullptr;
    dao_module* consumer_module = nullptr;
    const auto provider_load = dao_vm_load_module(vm, {loaded_provider.data(), loaded_provider.size()}, &provider_module, &error);
    if (provider_load != DAO_OK) { std::cerr << "provider load: " << error.message << "\n"; return EXIT_FAILURE; }
    const auto consumer_load = dao_vm_load_module(vm, {loaded_consumer.data(), loaded_consumer.size()}, &consumer_module, &error);
    if (consumer_load != DAO_OK) { std::cerr << "consumer load: " << error.message << "\n"; return EXIT_FAILURE; }
    const auto consumer_link = dao_vm_link_module(vm, consumer_module, &error);
    if (consumer_link != DAO_OK) { std::cerr << "consumer link: " << error.message << "\n"; return EXIT_FAILURE; }
    const auto provider_link = dao_vm_link_module(vm, provider_module, &error);
    if (provider_link != DAO_OK) { std::cerr << "provider link: " << error.message << "\n"; return EXIT_FAILURE; }
    if (dao_module_find_export(consumer_module, symbol("main"), &main_function) != DAO_OK ||
        dao_vm_call(vm, consumer_module, main_function, nullptr, 0, &result, &error) != DAO_OK ||
        result.type != DAO_VALUE_I64 || result.payload != 42) return EXIT_FAILURE;
    dao_module_release(provider_module);
    dao_module_release(consumer_module);
    dao_vm_destroy(vm);
    std::cout << "module store tests passed\n";
    std::filesystem::remove_all(root);
    return EXIT_SUCCESS;
}
