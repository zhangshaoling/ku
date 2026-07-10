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

#define CHECK(test_name, expression) check((expression), #expression, (test_name))

dao::Instruction instr(dao::Opcode opcode, uint16_t dst = 0, uint16_t a = 0, uint16_t b = 0,
                       int64_t immediate = 0) {
    return dao::Instruction{opcode, 0, dst, a, b, immediate};
}

std::vector<uint8_t> make_reference_module() {
    dao::ModuleBuilder builder;
    const uint32_t import_index = builder.add_import(700, 2);

    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {instr(dao::Opcode::AddI64, 2, 0, 1), instr(dao::Opcode::Return, 0, 2)};
    const uint32_t add_function = builder.add_function(std::move(add));
    builder.add_export(100, add_function);

    dao::FunctionSpec wrapper;
    wrapper.parameter_count = 2;
    wrapper.register_count = 3;
    wrapper.code = {instr(dao::Opcode::CallHost, 2, 0, 2, import_index),
                    instr(dao::Opcode::Return, 0, 2)};
    const uint32_t wrapper_function = builder.add_function(std::move(wrapper));
    builder.add_export(101, wrapper_function);

    dao::FunctionSpec branch;
    branch.parameter_count = 1;
    branch.register_count = 2;
    branch.code = {instr(dao::Opcode::BranchTritZero, 0, 0, 0, 3),
                   instr(dao::Opcode::LoadI64, 1, 0, 0, 1),
                   instr(dao::Opcode::Return, 0, 1),
                   instr(dao::Opcode::LoadI64, 1, 0, 0, 0),
                   instr(dao::Opcode::Return, 0, 1)};
    const uint32_t branch_function = builder.add_function(std::move(branch));
    builder.add_export(102, branch_function);

    return builder.encode();
}

std::string make_reference_text() {
    const auto bytes = make_reference_module();
    dao::DisassembledModule module{};
    dao_error error{};
    if (dao::disassemble({bytes.data(), bytes.size()}, &module, &error) != DAO_OK) {
        return {};
    }
    return dao::to_text(module);
}

// DAO assembler text (exact disasm format, no address column)
const char* dao_sample_text = R"DAO(
imports: 1
  [import 700] params=2
exports: 3
  symbol=100 -> function 0
  symbol=101 -> function 1
  symbol=102 -> function 2
functions: 3
  [function 0] registers=3 parameters=2 instructions=2
    0 ADD_I64 r2, r0, r1
    1 RETURN r2
  [function 1] registers=3 parameters=2 instructions=2
    0 CALL_HOST import0, args r0..r1, dst r2
    1 RETURN r2
  [function 2] registers=2 parameters=1 instructions=5
    0 BR_TRIT_ZERO r0, ->3
    1 LOAD_I64 r1, 1
    2 RETURN r1
    3 LOAD_I64 r1, 0
    4 RETURN r1
)DAO";

void test_parse_then_encode_matches_builder_round_trip() {
    const std::string text = make_reference_text();
    CHECK("reference text non-empty", !text.empty());

    dao::ParsedModule parsed{};
    dao_error perror{};
    CHECK("parse_module_text parses reference text",
          dao::parse_module_text(text, &parsed, &perror));
    CHECK("parsed has 1 import", parsed.imports.size() == 1);
    CHECK("parsed has 3 exports", parsed.exports.size() == 3);
    CHECK("parsed has 3 functions", parsed.functions.size() == 3);
    CHECK("parsed import symbol 700", parsed.imports[0].symbol_id == 700);
    CHECK("parsed import params 2", parsed.imports[0].parameter_count == 2);
    CHECK("parsed export 0 symbol 100", parsed.exports[0].symbol_id == 100);
    CHECK("parsed export 0 -> function 0", parsed.exports[0].function_index == 0);
    CHECK("parsed export 1 symbol 101", parsed.exports[1].symbol_id == 101);
    CHECK("parsed export 1 -> function 1", parsed.exports[1].function_index == 1);
    CHECK("parsed export 2 symbol 102", parsed.exports[2].symbol_id == 102);
    CHECK("parsed export 2 -> function 2", parsed.exports[2].function_index == 2);
    CHECK("parsed fn0 registers 3", parsed.functions[0].register_count == 3);
    CHECK("parsed fn0 params 2", parsed.functions[0].parameter_count == 2);
    CHECK("parsed fn0 code size 2", parsed.functions[0].code.size() == 2);
    CHECK("parsed fn0 instr0 opcode",
          parsed.functions[0].code[0].opcode == dao::Opcode::AddI64);
    CHECK("parsed fn0 instr0 dst 2", parsed.functions[0].code[0].dst == 2);
    CHECK("parsed fn0 instr0 a 0", parsed.functions[0].code[0].a == 0);
    CHECK("parsed fn0 instr0 b 1", parsed.functions[0].code[0].b == 1);
    CHECK("parsed fn0 instr1 opcode",
          parsed.functions[0].code[1].opcode == dao::Opcode::Return);
    CHECK("parsed fn0 instr1 a 2", parsed.functions[0].code[1].a == 2);
    CHECK("parsed fn1 instr0 opcode",
          parsed.functions[1].code[0].opcode == dao::Opcode::CallHost);
    CHECK("parsed fn1 instr0 immediate 0", parsed.functions[1].code[0].immediate == 0);
    CHECK("parsed fn1 instr0 a 0", parsed.functions[1].code[0].a == 0);
    CHECK("parsed fn1 instr0 b 2", parsed.functions[1].code[0].b == 2);
    CHECK("parsed fn1 instr0 dst 2", parsed.functions[1].code[0].dst == 2);
    CHECK("parsed fn2 instr0 opcode",
          parsed.functions[2].code[0].opcode == dao::Opcode::BranchTritZero);
    CHECK("parsed fn2 instr0 a 0", parsed.functions[2].code[0].a == 0);
    CHECK("parsed fn2 instr0 imm 3", parsed.functions[2].code[0].immediate == 3);
    CHECK("parsed fn2 instr1 imm 1", parsed.functions[2].code[1].immediate == 1);
    CHECK("parsed fn2 instr3 imm 0", parsed.functions[2].code[3].immediate == 0);

    // Now parse the fixed DAO sample text
    dao::ParsedModule parsed_sample{};
    dao_error perror_sample{};
    CHECK("dao_sample_text parses",
          dao::parse_module_text(std::string(dao_sample_text), &parsed_sample, &perror_sample));
    CHECK("parsed_sample 1 import", parsed_sample.imports.size() == 1);
    CHECK("parsed_sample 3 exports", parsed_sample.exports.size() == 3);
    CHECK("parsed_sample 3 functions", parsed_sample.functions.size() == 3);
    CHECK("parsed_sample fn0 instr0 CALL check opcode",
          parsed_sample.functions[1].code[0].opcode == dao::Opcode::CallHost);
    CHECK("parsed_sample fn0 instr0 CALL a0",
          parsed_sample.functions[1].code[0].a == 0);
    CHECK("parsed_sample fn0 instr0 CALL b2",
          parsed_sample.functions[1].code[0].b == 2);
    CHECK("parsed_sample fn0 instr0 CALL dst2",
          parsed_sample.functions[1].code[0].dst == 2);
}

void test_full_text_to_binary_matches_builder_round_trip() {
    const std::string text = make_reference_text();
    std::vector<uint8_t> asm_bytes;
    dao_error aerror{};
    CHECK("assemble_text produces bytes",
          dao::assemble_text(text, &asm_bytes, &aerror));
    const auto built_bytes = make_reference_module();
    CHECK("byte-for-byte match module builder (reference text)",
          asm_bytes.size() == built_bytes.size() &&
              std::equal(asm_bytes.begin(), asm_bytes.end(), built_bytes.begin()));

    std::vector<uint8_t> asm_bytes2;
    dao_error aerror2{};
    CHECK("assemble_text DAO sample text",
          dao::assemble_text(std::string(dao_sample_text), &asm_bytes2, &aerror2));
    CHECK("byte-for-byte match module builder (sample text)",
          asm_bytes2.size() == built_bytes.size() &&
              std::equal(asm_bytes2.begin(), asm_bytes2.end(), built_bytes.begin()));
}

void test_arithmetic_and_data_movement() {
    const std::string text = R"DAO(
imports: 0
exports: 1
  symbol=1 -> function 0
functions: 1
  [function 0] registers=5 parameters=3 instructions=6
    0 LOAD_I64 r3, 10
    1 LOAD_I64 r4, 20
    2 ADD_I64 r2, r0, r1
    3 MUL_I64 r2, r2, r3
    4 SUB_I64 r2, r2, r4
    5 RETURN r2
)DAO";
    std::vector<uint8_t> asm_bytes;
    dao_error aerror{};
    CHECK("dao arithmetic text assembles", dao::assemble_text(text, &asm_bytes, &aerror));
    dao::DisassembledModule mod{};
    CHECK("re-dissemble arithmetic",
          dao::disassemble({asm_bytes.data(), asm_bytes.size()}, &mod, &aerror) == DAO_OK);
    CHECK("number of instructions", mod.functions[0].instructions.size() == 6);
    CHECK("fn0 opcode0 LOAD_I64",
          mod.functions[0].instructions[0].opcode == dao::Opcode::LoadI64);
    CHECK("fn0 opcode0 imm 10", mod.functions[0].instructions[0].immediate == 10);
    CHECK("fn0 opcode2 ADD_I64",
          mod.functions[0].instructions[2].opcode == dao::Opcode::AddI64);
    CHECK("fn0 opcode2 dst 2", mod.functions[0].instructions[2].dst == 2);
    CHECK("fn0 opcode2 a 0", mod.functions[0].instructions[2].a == 0);
    CHECK("fn0 opcode2 b 1", mod.functions[0].instructions[2].b == 1);
    CHECK("fn0 opcode3 MUL_I64",
          mod.functions[0].instructions[3].opcode == dao::Opcode::MulI64);
}

void test_call_instruction() {
    const std::string text = R"DAO(
imports: 0
exports: 2
  symbol=0 -> function 0
  symbol=1 -> function 1
functions: 2
  [function 0] registers=2 parameters=0 instructions=0
  [function 1] registers=3 parameters=2 instructions=2
    0 ADD_I64 r2, r0, r1
    1 CALL fn0, args r2..r2, dst r2
)DAO";
    std::vector<uint8_t> asm_bytes;
    dao_error aerror{};
    CHECK("call sample text assembles", dao::assemble_text(text, &asm_bytes, &aerror));
    dao::DisassembledModule mod{};
    CHECK("call re-disassemble",
          dao::disassemble({asm_bytes.data(), asm_bytes.size()}, &mod, &aerror) == DAO_OK);
    CHECK("call instr opcode", mod.functions[1].instructions[1].opcode == dao::Opcode::Call);
    CHECK("call instr fn-index immediate 0",
          mod.functions[1].instructions[1].immediate == 0);
    CHECK("call instr dst 2", mod.functions[1].instructions[1].dst == 2);
    CHECK("call instr a 2", mod.functions[1].instructions[1].a == 2);
    CHECK("call instr b 1", mod.functions[1].instructions[1].b == 1);
}

void test_jump_and_negated_immediates() {
    const std::string text = R"DAO(
imports: 0
exports: 1
  symbol=0 -> function 0
functions: 1
  [function 0] registers=1 parameters=0 instructions=2
    0 JUMP ->5
    1 LOAD_I64 r0, -42
)DAO";
    std::vector<uint8_t> asm_bytes;
    dao_error aerror{};
    CHECK("jump + neg imm assemble", dao::assemble_text(text, &asm_bytes, &aerror));
    dao::DisassembledModule mod{};
    CHECK("jump + neg imm re-disassemble",
          dao::disassemble({asm_bytes.data(), asm_bytes.size()}, &mod, &aerror) == DAO_OK);
    CHECK("jump target 5", mod.functions[0].instructions[0].immediate == 5);
    CHECK("neg imm -42", mod.functions[0].instructions[1].immediate == -42);
}

void test_bad_input_rejected() {
    dao::ParsedModule parsed{};
    dao_error perror{};

    const std::string missing_counts = R"DAO(
imports
exports: 0
functions: 0
)DAO";
    CHECK("missing imports count rejected",
          !dao::parse_module_text(missing_counts, &parsed, &perror));

    parsed = {};
    const std::string empty_count = R"DAO(
imports:
exports: 0
functions: 0
)DAO";
    CHECK("empty imports count rejected",
          !dao::parse_module_text(empty_count, &parsed, &perror));

    parsed = {};
    const std::string garbage = R"DAO(
imports: 0
exports: 0
functions: 1
  [function 0] registers=1 parameters=0 instructions=1
    0 INVALIDE r0
)DAO";
    CHECK("invalid opcode rejected",
          !dao::parse_module_text(garbage, &parsed, &perror));

    std::vector<uint8_t> bytes2;
    dao_error err2{};
    const std::string overflow_text = R"DAO(
imports: 0
exports: 0
functions: 1
  [function 0] registers=1 parameters=0 instructions=1
    0 LOAD_I64 r0, 9999999999999999999999
)DAO";
    CHECK("integer overflow rejected",
          !dao::assemble_text(overflow_text, &bytes2, &err2));

    std::vector<uint8_t> bytes3;
    dao_error err3{};
    const std::string call_missing_args = R"DAO(
imports: 0
exports: 1
  symbol=0 -> function 0
functions: 1
  [function 0] registers=1 parameters=0 instructions=1
    0 CALL fn0, args
)DAO";
    CHECK("CALL without args range rejected",
          !dao::assemble_text(call_missing_args, &bytes3, &err3));
}

} // namespace

int main() {
    test_parse_then_encode_matches_builder_round_trip();
    test_full_text_to_binary_matches_builder_round_trip();
    test_arithmetic_and_data_movement();
    test_call_instruction();
    test_jump_and_negated_immediates();
    test_bad_input_rejected();

    if (failures != 0) {
        std::cerr << failures << " assembler test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "dao assembler tests passed\n";
    return EXIT_SUCCESS;
}
