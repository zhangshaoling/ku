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
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL " << test_name << ": " << expression << '\n';
}

#define CHECK(test_name, expression) check((expression), #expression, (test_name))

dao::Instruction instruction(dao::Opcode opcode, uint16_t dst = 0, uint16_t a = 0, uint16_t b = 0,
                             int64_t immediate = 0) {
    return dao::Instruction{opcode, 0, dst, a, b, immediate};
}

std::vector<uint8_t> make_module() {
    dao::ModuleBuilder builder;

    const uint32_t import_index = builder.add_import(700, 2);

    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {
        instruction(dao::Opcode::AddI64, 2, 0, 1),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t add_function = builder.add_function(std::move(add));
    builder.add_export(100, add_function);

    dao::FunctionSpec wrapper;
    wrapper.parameter_count = 2;
    wrapper.register_count = 3;
    wrapper.code = {
        instruction(dao::Opcode::CallHost, 2, 0, 2, import_index),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t wrapper_function = builder.add_function(std::move(wrapper));
    builder.add_export(101, wrapper_function);

    dao::FunctionSpec branch;
    branch.parameter_count = 1;
    branch.register_count = 2;
    branch.code = {
        instruction(dao::Opcode::BranchTritZero, 0, 0, 0, 3),
        instruction(dao::Opcode::LoadI64, 1, 0, 0, 1),
        instruction(dao::Opcode::Return, 0, 1),
        instruction(dao::Opcode::LoadI64, 1, 0, 0, 0),
        instruction(dao::Opcode::Return, 0, 1),
    };
    const uint32_t branch_function = builder.add_function(std::move(branch));
    builder.add_export(102, branch_function);

    return builder.encode();
}

void test_round_trip() {
    const auto bytes = make_module();
    dao::DisassembledModule module{};
    dao_error error{};
    CHECK("disassemble succeeds",
          dao::disassemble({bytes.data(), bytes.size()}, &module, &error) == DAO_OK);

    CHECK("three functions", module.functions.size() == 3);
    CHECK("one import", module.imports.size() == 1);
    CHECK("three exports", module.exports.size() == 3);

    CHECK("import symbol", module.imports[0].symbol_id == 700);
    CHECK("import params", module.imports[0].parameter_count == 2);

    CHECK("export 100 -> 0",
          module.exports[0].symbol_id == 100 && module.exports[0].function_index == 0);
    CHECK("export 101 -> 1",
          module.exports[1].symbol_id == 101 && module.exports[1].function_index == 1);
    CHECK("export 102 -> 2",
          module.exports[2].symbol_id == 102 && module.exports[2].function_index == 2);

    CHECK("fn0 registers", module.functions[0].register_count == 3);
    CHECK("fn0 params", module.functions[0].parameter_count == 2);
    CHECK("fn0 instruction count", module.functions[0].instructions.size() == 2);
    CHECK("fn0 first opcode", module.functions[0].instructions[0].opcode == dao::Opcode::AddI64);
    CHECK("fn0 first dst", module.functions[0].instructions[0].dst == 2);
    CHECK("fn0 first a", module.functions[0].instructions[0].a == 0);
    CHECK("fn0 first b", module.functions[0].instructions[0].b == 1);
    CHECK("fn0 second opcode", module.functions[0].instructions[1].opcode == dao::Opcode::Return);
    CHECK("fn0 second a", module.functions[0].instructions[1].a == 2);

    CHECK("fn1 has call_host", module.functions[1].instructions[0].opcode == dao::Opcode::CallHost);
    CHECK("fn1 call_host import", module.functions[1].instructions[0].immediate == 0);

    CHECK("fn2 first opcode",
          module.functions[2].instructions[0].opcode == dao::Opcode::BranchTritZero);
    CHECK("fn2 branch target", module.functions[2].instructions[0].immediate == 3);
    CHECK("fn2 second opcode", module.functions[2].instructions[1].opcode == dao::Opcode::LoadI64);
    CHECK("fn2 load immediate", module.functions[2].instructions[1].immediate == 1);
}

void test_round_trip_text() {
    const auto bytes = make_module();
    dao::DisassembledModule module{};
    dao_error error{};
    CHECK("disassemble for text",
          dao::disassemble({bytes.data(), bytes.size()}, &module, &error) == DAO_OK);
    std::string text = dao::to_text(module);
    CHECK("text contains import", text.find("[import 700]") != std::string::npos);
    CHECK("text contains export", text.find("symbol=100") != std::string::npos);
    CHECK("text contains fn0", text.find("[function 0]") != std::string::npos);
    CHECK("text contains ADD_I64", text.find("ADD_I64") != std::string::npos);
    CHECK("text contains CALL_HOST", text.find("CALL_HOST") != std::string::npos);
    CHECK("text not empty", !text.empty());
}

void test_bad_modules() {
    dao::DisassembledModule module{};
    dao_error error{};

    CHECK("null data rejected",
          dao::disassemble({nullptr, 100}, &module, &error) == DAO_BAD_MODULE);
    CHECK("too-small rejected", dao::disassemble({nullptr, 0}, &module, &error) == DAO_BAD_MODULE);

    uint8_t bad_magic[] = {'X', 'A', 'O', 0};
    CHECK("bad magic rejected",
          dao::disassemble({bad_magic, sizeof(bad_magic)}, &module, &error) == DAO_BAD_MODULE);

    uint8_t too_small[4] = {'D', 'A', 'O', 0};
    CHECK("small magic rejected",
          dao::disassemble({too_small, sizeof(too_small)}, &module, &error) == DAO_BAD_MODULE);

    const auto good = make_module();
    auto bad_version = good;
    bad_version[4] = 99;
    CHECK("bad version rejected", dao::disassemble({bad_version.data(), bad_version.size()},
                                                   &module, &error) == DAO_BAD_MODULE);

    CHECK("null out arg rejected",
          dao::disassemble({good.data(), good.size()}, nullptr, &error) == DAO_INVALID_ARGUMENT);
}

} // namespace

int main() {
    test_round_trip();
    test_round_trip_text();
    test_bad_modules();

    if (failures != 0) {
        std::cerr << failures << " disassembly test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "dao disassembly tests passed\n";
    return EXIT_SUCCESS;
}
