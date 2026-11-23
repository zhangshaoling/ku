#include "dao/dao.h"
#include "dao/format.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using O = dao::Opcode;
using VM = std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)>;
using Module = std::unique_ptr<dao_module, decltype(&dao_module_release)>;

void check(bool condition, const std::string& name) {
    if (!condition) throw std::runtime_error(name);
}
dao::Instruction op(O code, uint16_t dst = 0, uint16_t a = 0, uint16_t b = 0, int64_t imm = 0) {
    return {code, 0, dst, a, b, imm};
}
VM vm(bool records = true, uint64_t budget = 10000000) {
    auto config = dao_vm_config_default();
    config.max_instructions_per_call = budget;
    VM result(dao_vm_create(&config), dao_vm_destroy);
    check(result != nullptr, "create VM");
    if (records) check(dao_vm_set_features(result.get(), DAO_FEATURE_RECORDS) == DAO_OK, "enable Record");
    return result;
}
dao::ModuleBuilder point() {
    dao::ModuleBuilder builder;
    check(builder.add_record_type("Point") == 0, "type index");
    check(builder.add_record_field(0, "x", DAO_VALUE_I64) == 0, "field index");
    return builder;
}
void function(dao::ModuleBuilder& builder, std::vector<dao::Instruction> code,
              uint16_t registers = 8, uint16_t parameters = 0) {
    const auto index = builder.add_function({parameters, registers, std::move(code)});
    builder.add_export(3935363592u + index, index);
}
std::vector<dao::Instruction> construct() {
    return {op(O::LoadI64, 0, 0, 0, 42), op(O::MakeRecord, 1, 0, 1, 0)};
}
Module load(dao_vm* owner, const std::vector<uint8_t>& bytes, dao_status expected = DAO_OK) {
    dao_module* raw = nullptr;
    dao_error error{};
    const auto status = dao_vm_load_module(owner, {bytes.data(), bytes.size()}, &raw, &error);
    Module result(raw, dao_module_release);
    check(status == expected, "load: " + std::string(error.message));
    check((raw != nullptr) == (expected == DAO_OK), "load output");
    return result;
}
dao_value call(dao_vm* owner, dao_module* module, dao_status expected = DAO_OK,
               const std::vector<dao_value>& args = {}) {
    dao_value result{};
    dao_error error{};
    const auto status = dao_vm_call(owner, module, 0, args.data(), args.size(), &result, &error);
    check(status == expected, "call: " + std::string(dao_status_name(status)) + " " + error.message);
    return result;
}
std::string text(dao_bytes bytes) {
    return {reinterpret_cast<const char*>(bytes.data), bytes.size};
}
uint32_t u32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}
void put(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
size_t section(const std::vector<uint8_t>& bytes, uint32_t id) {
    for (size_t entry = 16; entry < 16 + u32(bytes, 12) * 16; entry += 16)
        if (u32(bytes, entry) == id) return u32(bytes, entry + 4);
    throw std::runtime_error("section missing");
}

void golden_and_lifetime(const char* fixture) {
    auto builder = point();
    auto code = construct(); code.push_back(op(O::Return, 0, 1));
    function(builder, code, 2);
    const auto bytes = builder.encode();
    std::ifstream input(fixture);
    std::vector<uint8_t> golden;
    unsigned int byte = 0;
    while (input >> std::hex >> byte) golden.push_back(static_cast<uint8_t>(byte));
    check(!golden.empty() && bytes == golden, "builder matches reviewed wire fixture exactly");
    auto a = vm(), b = vm(), disabled = vm(false);
    load(disabled.get(), bytes, DAO_UNSUPPORTED_FEATURE);
    auto module = load(a.get(), bytes);
    auto cached = load(a.get(), bytes);
    check(cached.get() == module.get(), "verified cache reuse");
    uint64_t features = 99;
    check(dao_module_required_features(module.get(), &features) == DAO_OK && features == 1, "required features");
    check(dao_vm_set_features(a.get(), 0) == DAO_INVALID_ARGUMENT, "capability locked");
    check(dao_vm_set_features(a.get(), 1) == DAO_OK, "idempotent capability");
    check(dao_vm_set_features(disabled.get(), 2) == DAO_INVALID_ARGUMENT, "unknown capability");
    call(disabled.get(), module.get(), DAO_UNSUPPORTED_FEATURE);
    check(dao_vm_set_features(disabled.get(), 1) == DAO_OK, "failed call/load does not lock capabilities");
    const auto record = call(a.get(), module.get());
    auto other_module = load(b.get(), bytes);
    const auto other_record = call(b.get(), other_module.get());
    check(record.payload != other_record.payload, "Record IDs cannot alias across VMs");
    dao_value field{DAO_VALUE_I64, 0, 999};
    check(dao_value_record_get(b.get(), &record, 0, &field) == DAO_RUNTIME_ERROR && field.type == DAO_VALUE_NULL,
          "foreign Record rejected and output cleared");
    call(a.get(), module.get(), DAO_INVALID_ARGUMENT, {record});
    check(dao_value_record_get(a.get(), &record, 0, &field) == DAO_OK && field.payload == 42,
          "rejected top-level input preserves old generation");
    check(dao_value_record_get(a.get(), &record, 1, &field) == DAO_INVALID_ARGUMENT, "field index bounds");
    dao_bytes name{}; uint32_t count = 0, tag = 0;
    check(dao_module_record_count(module.get(), &count) == DAO_OK && count == 1, "type count");
    check(dao_module_record_name(module.get(), 0, &name) == DAO_OK && text(name) == "Point", "type name");
    check(dao_module_record_field_count(module.get(), 0, &count) == DAO_OK && count == 1, "field count");
    check(dao_module_record_field_info(module.get(), 0, 0, &name, &tag) == DAO_OK && text(name) == "x" && tag == 1,
          "field metadata");
    check(dao_module_record_field_info(module.get(), 0, 9, &name, &tag) == DAO_INVALID_ARGUMENT && name.size == 0 && tag == 0,
          "bad metadata query clears outputs");
    check(dao_vm_clear_module_cache(a.get()) == DAO_OK, "clear cache");
    cached.reset(); module.reset();
    check(dao_value_record_get(a.get(), &record, 0, &field) == DAO_OK && field.payload == 42,
          "arena keeps owner alive after release and cache clearing");
    dao_module* raw_owner = nullptr; uint32_t type = 99;
    check(dao_value_record_type(a.get(), &record, &raw_owner, &type) == DAO_OK && type == 0, "retained owner query");
    Module retained(raw_owner, dao_module_release);
    call(a.get(), retained.get());
    check(dao_value_record_get(a.get(), &record, 0, &field) == DAO_RUNTIME_ERROR, "next call invalidates Record");
    check(dao_value_record_type(a.get(), &record, &raw_owner, &type) == DAO_RUNTIME_ERROR && !raw_owner && type == 0,
          "stale owner query clears outputs");
}

void instruction_contracts() {
    for (const auto opcode : {O::RecordGet, O::FieldGet}) {
        auto builder = point();
        const auto name = builder.add_string("x");
        auto code = construct();
        code.push_back(op(opcode, 2, 1, 0, opcode == O::FieldGet ? name : 0));
        code.push_back(op(O::Return, 0, 2)); function(builder, code);
        auto owner = vm(); auto module = load(owner.get(), builder.encode());
        check(call(owner.get(), module.get()).payload == 42, "static/dynamic field read");
    }
    for (const auto opcode : {O::IndexGet, O::IndexSet, O::Length, O::MapKeys, O::CompareEqI64}) {
        auto builder = point(); auto code = construct();
        code.push_back(op(opcode, opcode == O::IndexSet ? 1 : 2, 1, 1));
        code.push_back(op(O::Return, 0, 2)); function(builder, code);
        auto owner = vm(); auto module = load(owner.get(), builder.encode());
        call(owner.get(), module.get(), DAO_TYPE_ERROR);
    }
    auto builder = point(); builder.add_record_type("Other"); builder.add_record_field(1, "x", 1);
    auto code = construct(); code.push_back(op(O::RecordGet, 2, 1, 0, 1)); code.push_back(op(O::Return, 0, 2));
    function(builder, code); auto owner = vm(); auto module = load(owner.get(), builder.encode());
    call(owner.get(), module.get(), DAO_TYPE_ERROR);

    auto missing = point(); const auto name = missing.add_string("missing");
    code = construct(); code.push_back(op(O::FieldGet, 2, 1, 0, name)); code.push_back(op(O::Return, 0, 2));
    function(missing, code); auto missing_module = load(owner.get(), missing.encode());
    call(owner.get(), missing_module.get(), DAO_TYPE_ERROR);

    dao::ModuleBuilder empty; empty.add_record_type("Empty");
    function(empty, {op(O::MakeRecord, 0), op(O::Return, 0, 0)}, 1);
    auto empty_module = load(owner.get(), empty.encode()); const auto value = call(owner.get(), empty_module.get());
    dao_value field{};
    check(dao_value_record_get(owner.get(), &value, 0, &field) == DAO_INVALID_ARGUMENT, "empty Record");

    auto alias = point(); function(alias, {op(O::LoadI64, 0, 0, 0, 42), op(O::MakeRecord, 0, 0, 1), op(O::Return, 0, 0)}, 1);
    auto alias_module = load(owner.get(), alias.encode()); const auto aliased = call(owner.get(), alias_module.get());
    check(dao_value_record_get(owner.get(), &aliased, 0, &field) == DAO_OK && field.payload == 42, "overlapping destination");
}

void scalar_fields_and_bad_descriptors() {
    dao::ModuleBuilder builder; builder.add_record_type("多值");
    for (uint32_t type = 1; type <= 4; ++type) builder.add_record_field(0, std::to_string(type), type);
    function(builder, {op(O::MakeRecord, 4, 0, 4), op(O::Return, 0, 4)}, 5, 4);
    auto owner = vm(); auto module = load(owner.get(), builder.encode());
    const std::string storage = "中文";
    dao_value bytes{}, string{};
    dao_value_make_bytes_view({reinterpret_cast<const uint8_t*>(storage.data()), storage.size()}, &bytes);
    dao_value_make_string_view({reinterpret_cast<const uint8_t*>(storage.data()), storage.size()}, &string);
    const auto result = call(owner.get(), module.get(), DAO_OK, {{DAO_VALUE_I64, 0, 42}, {DAO_VALUE_TRIT, 0, 0}, bytes, string});
    for (uint32_t index = 0; index < 4; ++index) {
        dao_value field{};
        check(dao_value_record_get(owner.get(), &result, index, &field) == DAO_OK && field.type == index + 1, "scalar field strict type");
    }
    auto encoded = builder.encode();
    const size_t fields = section(encoded, 9);
    put(encoded, fields + 12, u32(encoded, fields));
    load(owner.get(), encoded, DAO_BAD_MODULE);
    encoded = builder.encode(); put(encoded, section(encoded, 8) + 8, 257);
    load(owner.get(), encoded, DAO_BAD_MODULE);
    bool rejected = false;
    try { builder.add_record_type("多值"); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "duplicate type rejected by builder");
    rejected = false;
    try { builder.add_record_field(0, "bad", DAO_VALUE_MAP); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "container field rejected by builder");
}

dao_status count_host(void* data, const dao_value*, size_t, dao_value* out) {
    ++*static_cast<int*>(data); *out = {}; return DAO_OK;
}

void host_and_module_boundaries() {
    for (bool wrapped : {false, true}) {
        auto builder = point(); builder.add_import(7, 1);
        auto code = construct();
        if (wrapped) code.push_back(op(O::MakeList, 2, 1, 1));
        code.push_back(op(O::CallHost, 3, wrapped ? 2 : 1, 1, 0)); code.push_back(op(O::Return, 0, 3));
        function(builder, code); auto owner = vm(); auto module = load(owner.get(), builder.encode());
        int calls = 0; const dao_host_function host{sizeof(host), 7, 1, 0, count_host, &calls};
        check(dao_vm_register_host_function(owner.get(), &host) == DAO_OK, "register host");
        call(owner.get(), module.get(), DAO_TYPE_ERROR); check(calls == 0, "Host sees no Record, including nested");
    }
    for (bool throws : {false, true}) {
        auto provider = point(); provider.set_identity("ku:test/records", {1, 0, 0});
        auto code = construct(); code.push_back(op(O::MakeList, 2, 1, 1));
        code.push_back(op(throws ? O::Throw : O::Return, 0, 2)); function(provider, code);
        dao::ModuleBuilder client; client.set_identity("ku:test/client", {1, 0, 0});
        client.add_module_import("ku:test/records", {1, 0, 0}, 3935363592u, 0);
        function(client, {op(O::TryBegin, 0, 0, 0, 4), op(O::CallModule, 0), op(O::TryEnd),
                          op(O::Return, 0, 0), op(O::Catch, 1), op(O::Return, 0, 1)});
        auto owner = vm(); auto target = load(owner.get(), provider.encode()); auto caller = load(owner.get(), client.encode());
        dao_error error{};
        check(dao_vm_link_module(owner.get(), target.get(), &error) == DAO_OK, "link typed provider");
        call(owner.get(), caller.get(), DAO_TYPE_ERROR);
        auto disabled = vm(false);
        check(dao_vm_link_module(disabled.get(), target.get(), &error) == DAO_UNSUPPORTED_FEATURE, "link checks target VM features");
    }
    auto builder = point(); builder.add_import(7, 1);
    function(builder, {op(O::LoadI64, 0), op(O::MakeList, 1, 0, 1), op(O::CallHost, 2, 1, 1), op(O::Return, 0, 2)});
    auto limited = vm(true, 4); auto module = load(limited.get(), builder.encode());
    int calls = 0; const dao_host_function host{sizeof(host), 7, 1, 0, count_host, &calls};
    dao_vm_register_host_function(limited.get(), &host);
    call(limited.get(), module.get(), DAO_INSTRUCTION_LIMIT_EXCEEDED);
    check(calls == 0, "boundary traversal budget exhausts before Host side effects");
}

dao_status invalid_record_result(void*, const dao_value*, size_t, dao_value* out) {
    *out = {DAO_VALUE_RECORD, 0, 123};
    return DAO_OK;
}

void graph_and_input_boundaries() {
    auto owner = vm();
    int calls = 0;
    const dao_host_function host{sizeof(host), 7, 1, 0, count_host, &calls};
    dao_vm_register_host_function(owner.get(), &host);
    auto map_builder = point(); map_builder.add_import(7, 1);
    const auto name = map_builder.add_string("wrapped");
    auto code = construct();
    code.push_back(op(O::LoadString, 2, 0, 0, name)); code.push_back(op(O::Move, 3, 1));
    code.push_back(op(O::MakeMap, 4, 2, 1)); code.push_back(op(O::CallHost, 5, 4, 1));
    code.push_back(op(O::Return, 0, 5)); function(map_builder, code);
    auto wrapped = load(owner.get(), map_builder.encode());
    call(owner.get(), wrapped.get(), DAO_TYPE_ERROR); check(calls == 0, "Map cannot conceal Record at Host boundary");

    auto cycle_builder = point(); cycle_builder.add_import(7, 1);
    function(cycle_builder, {op(O::MakeList, 0), op(O::ListAppend, 0, 0),
                            op(O::CallHost, 1, 0, 1), op(O::Return, 0, 1)});
    auto cycle = load(owner.get(), cycle_builder.encode());
    call(owner.get(), cycle.get()); check(calls == 1, "cycle without Record terminates boundary scan");

    auto fields = point(); const auto missing_name = fields.add_string("missing");
    function(fields, {op(O::MakeMap, 0), op(O::FieldGet, 1, 0, 0, missing_name), op(O::Return, 0, 1)});
    auto map = load(owner.get(), fields.encode());
    check(call(owner.get(), map.get()).type == DAO_VALUE_NULL, "dynamic Map field still returns null");

    const dao_host_function bad_host{sizeof(bad_host), 8, 0, 0, invalid_record_result, nullptr};
    dao_vm_register_host_function(owner.get(), &bad_host);
    auto bad_builder = point(); bad_builder.add_import(8, 0);
    function(bad_builder, {op(O::CallHost, 0), op(O::Return, 0, 0)});
    auto bad = load(owner.get(), bad_builder.encode());
    call(owner.get(), bad.get(), DAO_TYPE_ERROR);

    for (bool nested : {false, true}) {
        dao::ModuleBuilder provider; provider.set_identity("ku:test/receiver", {1, 0, 0}); provider.add_import(7, 0);
        function(provider, {op(O::CallHost, 1), op(O::Return, 0, 0)}, 2, 1);
        auto client = point(); client.set_identity("ku:test/sender", {1, 0, 0});
        client.add_module_import("ku:test/receiver", {1, 0, 0}, 3935363592u, 1);
        code = construct(); if (nested) code.push_back(op(O::MakeList, 2, 1, 1));
        code.push_back(op(O::CallModule, 3, nested ? 2 : 1, 1)); code.push_back(op(O::Return, 0, 3));
        function(client, code);
        auto caller_vm = vm(); auto target = load(caller_vm.get(), provider.encode());
        auto caller = load(caller_vm.get(), client.encode());
        dao_error error{}; calls = 0;
        const dao_host_function counter{sizeof(counter), 7, 0, 0, count_host, &calls};
        dao_vm_register_host_function(caller_vm.get(), &counter);
        check(dao_vm_link_module(caller_vm.get(), target.get(), &error) == DAO_OK, "link receiver");
        call(caller_vm.get(), caller.get(), DAO_TYPE_ERROR);
        check(calls == 0, "module input rejection happens before callee executes");
    }
}

void verifier_and_legacy() {
    auto owner = vm();
    for (auto instruction : {op(O::RecordGet, 2, 1, 1), op(O::RecordGet, 2, 1, 0, -1),
                             op(O::FieldGet, 2, 1, 1), op(O::FieldGet, 2, 1, 0, 999)}) {
        auto builder = point(); auto code = construct(); code.push_back(instruction);
        code.push_back(op(O::Return, 0, 2)); function(builder, code);
        load(owner.get(), builder.encode(), DAO_VERIFY_ERROR);
    }
    dao::ModuleBuilder legacy;
    function(legacy, {op(O::LoadI64, 0, 0, 0, 7), op(O::Return, 0, 0)}, 1);
    auto bytes = legacy.encode();
    auto disabled = vm(false); auto module = load(disabled.get(), bytes);
    check(dao_vm_set_features(disabled.get(), 1) == DAO_INVALID_ARGUMENT, "legacy load locks capability policy");
    uint32_t count = 99;
    check(dao_module_record_count(module.get(), &count) == DAO_OK && count == 0, "old module has no Record types");
    check(call(disabled.get(), module.get()).payload == 7, "old bytecode still executes without opt-in");
    bytes[section(bytes, 2)] = static_cast<uint8_t>(O::MakeRecord);
    load(owner.get(), bytes, DAO_VERIFY_ERROR);

    auto builder = point(); auto code = construct(); code.push_back(op(O::Return, 0, 1)); function(builder, code);
    bytes = builder.encode();
    // A zero-sized IMPORT between real sections must not hide their overlap.
    const auto data_offset = section(bytes, 5);
    put(bytes, 16 + 5 * 16 + 4, static_cast<uint32_t>(data_offset));
    load(owner.get(), bytes, DAO_BAD_MODULE);
    auto typed = load(owner.get(), builder.encode());
    const auto record = call(owner.get(), typed.get());
    dao_value field{};
    check(dao_value_record_get(disabled.get(), &record, 0, &field) == DAO_UNSUPPORTED_FEATURE, "queries require opt-in");
    check(dao_value_record_get(nullptr, &record, 0, &field) == DAO_INVALID_ARGUMENT, "query validates VM");
    check(dao_value_record_get(owner.get(), nullptr, 0, &field) == DAO_INVALID_ARGUMENT, "query validates value");
    check(dao_value_record_get(owner.get(), &record, 0, nullptr) == DAO_INVALID_ARGUMENT, "query validates output");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    try {
        golden_and_lifetime(argv[1]);
        instruction_contracts();
        scalar_fields_and_bad_descriptors();
        host_and_module_boundaries();
        graph_and_input_boundaries();
        verifier_and_legacy();
    } catch (const std::exception& error) {
        std::cerr << "FAIL Record: " << error.what() << '\n'; return EXIT_FAILURE;
    }
    std::cout << "Record construction, fields, ownership and boundaries passed\n";
    return EXIT_SUCCESS;
}
