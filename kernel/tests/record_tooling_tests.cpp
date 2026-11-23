#include "dao/assemble.hpp"
#include "dao/disassemble.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}

dao::Instruction op(dao::Opcode opcode, uint16_t dst = 0, uint16_t a = 0,
                    uint16_t b = 0, int64_t immediate = 0) {
    return {opcode, 0, dst, a, b, immediate};
}

std::vector<uint8_t> record_module(bool identified) {
    dao::ModuleBuilder builder;
    if (identified) {
        builder.set_identity("ku:test/record-tools", {1, 2, 3});
        builder.add_module_import("ku:test/provider", {2, 0, 1}, 7, 0);
    }
    const auto label = builder.add_string("中文\n\"text");
    const auto x = builder.add_string("横");
    builder.add_string("");
    check(builder.add_string("横") == x, "shared DATA name index");
    const auto point = builder.add_record_type("点");
    builder.add_record_field(point, "横", DAO_VALUE_I64);
    builder.add_record_field(point, "有效", DAO_VALUE_TRIT);
    builder.add_record_field(point, "名称", DAO_VALUE_STRING);
    const auto empty = builder.add_record_type("空型");
    const auto binary = builder.add_record_type("字节型");
    builder.add_record_field(binary, "数据", DAO_VALUE_BYTES);
    dao::FunctionSpec fn;
    fn.register_count = 9;
    fn.code = {op(dao::Opcode::LoadI64, 0, 0, 0, 42),
               op(dao::Opcode::LoadTrit, 1, 0, 0, 1),
               op(dao::Opcode::LoadString, 2, 0, 0, label),
               op(dao::Opcode::MakeRecord, 3, 0, 3, point),
               op(dao::Opcode::RecordGet, 4, 3, 0, point),
               op(dao::Opcode::FieldGet, 5, 3, 0, x),
               op(dao::Opcode::MakeRecord, 6, 0, 0, empty),
               op(dao::Opcode::AddI64, 7, 4, 5),
               op(dao::Opcode::Return, 0, 7)};
    builder.add_export(1, builder.add_function(std::move(fn)));
    return builder.encode();
}

void round_trip(bool identified) {
    const auto expected = record_module(identified);
    dao::DisassembledModule module;
    dao_error error{};
    if (dao::disassemble({expected.data(), expected.size()}, &module, &error) != DAO_OK) {
        check(false, "disassemble Record module"); return;
    }
    const auto text = dao::to_text(module);
    check(text.starts_with("format: 3 abi=11\n"), "explicit ABI11 text version");
    check(text.find("record_types: 3\n") != std::string::npos, "all Record descriptors");
    check(text.find("MAKE_RECORD") != std::string::npos &&
          text.find("RECORD_GET") != std::string::npos &&
          text.find("FIELD_GET") != std::string::npos, "three Record instructions");
    std::vector<uint8_t> actual;
    if (!dao::assemble_text(text, &actual, &error)) {
        std::cerr << error.message << '\n'; check(false, "assemble Record text"); return;
    }
    check(actual == expected, "canonical Record binary round trip");
    dao::DisassembledModule again;
    check(dao::disassemble({actual.data(), actual.size()}, &again, &error) == DAO_OK &&
          dao::to_text(again) == text, "stable Record text round trip");
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    dao_module* loaded = nullptr;
    dao_value result{};
    check(vm && dao_vm_set_features(vm, DAO_FEATURE_RECORDS) == DAO_OK &&
          dao_vm_load_module(vm, {actual.data(), actual.size()}, &loaded, &error) == DAO_OK &&
          dao_vm_call(vm, loaded, 0, nullptr, 0, &result, &error) == DAO_OK &&
          result.type == DAO_VALUE_I64 && result.payload == 84, "execute reassembled module");
    if (loaded) dao_module_release(loaded);
    dao_vm_destroy(vm);
}

void malformed_text() {
    const auto bytes = record_module(false);
    dao::DisassembledModule module;
    dao_error error{};
    if (dao::disassemble({bytes.data(), bytes.size()}, &module, &error) != DAO_OK) return;
    const auto original = dao::to_text(module);
    struct Mutation { const char* from; const char* to; };
    const Mutation mutations[] = {
        {"format: 3 abi=11", "format: 3 abi=10"},
        {"format: 3 abi=11", "format: 3 abi=11\nformat: 3 abi=11"},
        {"format: 3 abi=11\n", ""},
        {"record_types: 3", "record_types: 4"},
        {"record_types: 3", "record_types: 0"},
        {"[record 0]", "[record 1]"},
        {"fields=3", "fields=2"},
        {"fields=3", "fields=4"},
        {"type=1", "type=9"},
        {"[record 0] name=3 fields=3", "[record 0] name=2 fields=3"},
        {"[record 1] name=6", "[record 1] name=3"},
        {"field name=4 type=2", "field name=1 type=2"},
        {"[record 1]", ",\n[record 1]"},
        {"name=4", "name=4294967295"},
        {"MAKE_RECORD r3, r0, 3, 0", "MAKE_RECORD r3, r0, 2, 0"},
        {"RECORD_GET r4, r3, 0, 0", "RECORD_GET r4, r3, 9, 0"},
        {"FIELD_GET r5, r3, 0, 1", "FIELD_GET r5, r3, 1, 1"},
        {"FIELD_GET r5, r3, 0, 1", "FIELD_GET r5, r3, 0, -1"},
        {"FIELD_GET r5, r3, 0, 1", "FIELD_GET r5, r3, 0, 1 extra"},
        {"MAKE_RECORD r3, r0, 3, 0", "MAKE_RECORD r3, r0, 3, 4294967296"},
        {"MAKE_RECORD r3, r0, 3, 0", "MAKE_RECORD r65536, r0, 3, 0"},
        {"MAKE_RECORD r3, r0, 3, 0", "MAKE_RECORD r3, r0, 65536, 0"},
        {"MAKE_RECORD r3, r0, 3, 0", "MAKE_RECORD r3, r0, 3, -9223372036854775808"},
        {"FIELD_GET r5, r3, 0, 1", "FIELD_GET r5, r3, 0"},
    };
    for (const auto& mutation : mutations) {
        auto text = original;
        const auto at = text.find(mutation.from);
        check(at != std::string::npos, mutation.from);
        if (at == std::string::npos) continue;
        text.replace(at, std::char_traits<char>::length(mutation.from), mutation.to);
        std::vector<uint8_t> output;
        check(!dao::assemble_text(text, &output, &error), mutation.to);
    }
}

void legacy_versions() {
    for (bool identified : {false, true}) {
        dao::ModuleBuilder builder;
        if (identified) builder.set_identity("ku:test/legacy", {1, 0, 0});
        dao::FunctionSpec fn;
        fn.register_count = 1;
        fn.code = {op(dao::Opcode::LoadI64, 0, 0, 0, 42), op(dao::Opcode::Return, 0, 0)};
        builder.add_export(1, builder.add_function(std::move(fn)));
        const auto expected = builder.encode();
        dao::DisassembledModule module;
        dao_error error{};
        check(dao::disassemble({expected.data(), expected.size()}, &module, &error) == DAO_OK &&
              module.format_version == (identified ? 2 : 1) &&
              module.vm_abi_version == (identified ? 10 : 9), "preserve legacy model versions");
        const auto text = dao::to_text(module);
        check(!text.starts_with("format:"), "legacy text unchanged");
        std::vector<uint8_t> actual;
        check(dao::assemble_text(text, &actual, &error) && actual == expected,
              "legacy exact byte round trip");
    }
}

void malformed_binary() {
    const auto good = record_module(false);
    dao_error error{};
    for (size_t length = 0; length < good.size(); ++length) {
        dao::DisassembledModule module;
        check(dao::disassemble({good.data(), length}, &module, &error) != DAO_OK,
              "truncated Record module rejected");
    }
    // Section table is canonical: Functions, Code, Exports, Imports, Data, Types, Fields.
    auto invalid = good;
    invalid[16 + 5 * 16 + 8] = 1; // RecordTypes byte size.
    dao::DisassembledModule module;
    check(dao::disassemble({invalid.data(), invalid.size()}, &module, &error) != DAO_OK,
          "invalid type table rejected");
    check(dao::disassemble({good.data(), good.size()}, &module, &error) == DAO_OK &&
          dao::disassemble({good.data(), good.size()}, &module, &error) == DAO_OK,
          "output object can be reused");
    std::vector<uint8_t> assembled;
    check(dao::assemble_text(dao::to_text(module), &assembled, &error) && assembled == good,
          "reused output has no accumulated descriptors");
}
} // namespace

int main() {
    round_trip(false);
    round_trip(true);
    malformed_text();
    malformed_binary();
    legacy_versions();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
