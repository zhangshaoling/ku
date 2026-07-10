#include "dao/ku_migration.hpp"
#include "dao/assemble.hpp"
#include "dao/dao.h"
#include "dao/disassemble.hpp"
#include "dao/format.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* test_name) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL " << test_name << ": " << expression << '\n';
}

#define CHECK(test_name, ...) check((__VA_ARGS__), #__VA_ARGS__, (test_name))

std::vector<uint8_t> make_reference_module() {
    dao::ModuleBuilder builder;
    builder.add_import(700, 2);

    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {dao::Instruction{dao::Opcode::AddI64, 0, 2, 0, 1},
                dao::Instruction{dao::Opcode::Return, 0, 0, 2, 0}};
    const uint32_t add_function = builder.add_function(std::move(add));
    builder.add_export(100, add_function);

    dao::FunctionSpec wrapper;
    wrapper.parameter_count = 2;
    wrapper.register_count = 3;
    wrapper.code = {dao::Instruction{dao::Opcode::CallHost, 0, 2, 0, 2, 0},
                    dao::Instruction{dao::Opcode::Return, 0, 0, 2, 0}};
    const uint32_t wrapper_function = builder.add_function(std::move(wrapper));
    builder.add_export(101, wrapper_function);

    dao::FunctionSpec branch;
    branch.parameter_count = 1;
    branch.register_count = 2;
    branch.code = {dao::Instruction{dao::Opcode::BranchTritZero, 0, 0, 0, 0, 3},
                   dao::Instruction{dao::Opcode::LoadI64, 0, 1, 0, 0, 1},
                   dao::Instruction{dao::Opcode::Return, 0, 0, 1, 0},
                   dao::Instruction{dao::Opcode::LoadI64, 0, 1, 0, 0, 0},
                   dao::Instruction{dao::Opcode::Return, 0, 0, 1, 0}};
    const uint32_t branch_function = builder.add_function(std::move(branch));
    builder.add_export(102, branch_function);

    return builder.encode();
}

void test_smoke_emission_matches_builder() {
    const std::vector<uint8_t> reference = make_reference_module();
    CHECK("reference module is non-empty", !reference.empty());

    dao::ModuleBuilder builder;
    dao_error error{};
    CHECK("km::compile returns DAO_OK",
          dao::km::compile("module{ add = function(a,b){ a+b } }\n", &builder, &error) == DAO_OK);

    std::vector<uint8_t> produced = builder.encode();
    CHECK("migration output matches handmade builder byte-for-byte", produced == reference);
}

void test_disassemble_round_trip() {
    const std::vector<uint8_t> reference = make_reference_module();
    dao::DisassembledModule dm{};
    dao_error derr{};
    CHECK("disassemble reference module",
          dao::disassemble({reference.data(), reference.size()}, &dm, &derr) == DAO_OK);
    CHECK("three functions emitted", dm.functions.size() == 3);
    CHECK("one import emitted", dm.imports.size() == 1);
    CHECK("import is symbol 700", dm.imports[0].symbol_id == 700);
    CHECK("three exports emitted", dm.exports.size() == 3);
    CHECK("export 0 symbol 100", dm.exports[0].symbol_id == 100);
    CHECK("export 1 symbol 101", dm.exports[1].symbol_id == 101);
    CHECK("export 2 symbol 102", dm.exports[2].symbol_id == 102);
    CHECK("function 0 has 2 instructions", dm.functions[0].instructions.size() == 2);
    CHECK("function 0 instruction 0 is ADD_I64",
          dm.functions[0].instructions[0].opcode == dao::Opcode::AddI64);
    CHECK("function 0 instruction 0 dst=2", dm.functions[0].instructions[0].dst == 2);
    CHECK("function 0 instruction 0 a=0", dm.functions[0].instructions[0].a == 0);
    CHECK("function 0 instruction 0 b=1", dm.functions[0].instructions[0].b == 1);
    CHECK("function 0 instruction 1 is RETURN",
          dm.functions[0].instructions[1].opcode == dao::Opcode::Return);
    CHECK("function 0 instruction 1 a=2", dm.functions[0].instructions[1].a == 2);
    CHECK("function 1 has 2 instructions", dm.functions[1].instructions.size() == 2);
    CHECK("function 1 instruction 0 is CALL_HOST",
          dm.functions[1].instructions[0].opcode == dao::Opcode::CallHost);
    CHECK("function 1 instruction 0 import immediate 0",
          dm.functions[1].instructions[0].immediate == 0);
    CHECK("function 2 has 5 instructions", dm.functions[2].instructions.size() == 5);
    CHECK("function 2 instruction 0 BR_TRIT_ZERO",
          dm.functions[2].instructions[0].opcode == dao::Opcode::BranchTritZero);
    CHECK("function 2 instruction 0 branch target 3",
          dm.functions[2].instructions[0].immediate == 3);
    CHECK("function 2 instruction 2 RETURN",
          dm.functions[2].instructions[2].opcode == dao::Opcode::Return);
}

void test_null_builder_rejected() {
    dao_error error{};
    CHECK("null builder returns DAO_INVALID_ARGUMENT",
          dao::km::compile("ignored", nullptr, &error) == DAO_INVALID_ARGUMENT);
}

void test_source_ignored_still_emits_smoke() {
    const std::vector<uint8_t> reference = make_reference_module();
    dao::ModuleBuilder builder;
    dao_error error{};
    CHECK("garbage source still produces smoke module",
          dao::km::compile("garbage$not%real", &builder, &error) == DAO_OK);
    std::vector<uint8_t> produced = builder.encode();
    CHECK("ignoring source still matches reference", produced == reference);
}

} // namespace

int main() {
    test_smoke_emission_matches_builder();
    test_disassemble_round_trip();
    test_null_builder_rejected();
    test_source_ignored_still_emits_smoke();

    if (failures != 0) {
        std::cerr << failures << " K4 migration test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "dao K4 migration tests passed\n";
    return EXIT_SUCCESS;
}
