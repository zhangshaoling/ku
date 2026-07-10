#include "dao/dao.h"
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

dao_module* load_module(dao_vm* vm, const std::vector<uint8_t>& bytes, dao_error* error) {
    dao_module* module = nullptr;
    const dao_status status =
        dao_vm_load_module(vm, dao_bytes{bytes.data(), bytes.size()}, &module, error);
    return status == DAO_OK ? module : nullptr;
}

void write_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<uint8_t>(value >> shift);
    }
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(bytes[offset++]) << shift;
    }
    return value;
}

std::vector<uint8_t> make_add_module(uint32_t symbol_id) {
    dao::ModuleBuilder builder;
    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {
        instruction(dao::Opcode::AddI64, 2, 0, 1),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t function = builder.add_function(std::move(add));
    builder.add_export(symbol_id, function);
    return builder.encode();
}

dao_status host_add_bias(void* user_data, const dao_value* args, size_t arg_count,
                         dao_value* out_value) {
    if (arg_count != 2 || args == nullptr || out_value == nullptr ||
        args[0].type != DAO_VALUE_I64 || args[1].type != DAO_VALUE_I64) {
        return DAO_INVALID_ARGUMENT;
    }
    const int64_t bias = *static_cast<const int64_t*>(user_data);
    *out_value = {DAO_VALUE_I64, 0, args[0].payload + args[1].payload + bias};
    return DAO_OK;
}

dao_status host_invalid_value(void*, const dao_value*, size_t, dao_value* out_value) {
    *out_value = {DAO_VALUE_TRIT, 0, 7};
    return DAO_OK;
}

dao_status host_error(void*, const dao_value*, size_t, dao_value*) { return DAO_DIVIDE_BY_ZERO; }

dao_status host_throws(void*, const dao_value*, size_t, dao_value*) { throw 42; }

void test_deterministic_encoding() {
    const auto first = make_add_module(100);
    const auto second = make_add_module(100);
    CHECK("deterministic encoding", first == second);
}

void test_add_and_export(dao_vm* vm) {
    const auto bytes = make_add_module(100);
    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load add module", module != nullptr);
    if (module == nullptr)
        return;

    dao_function function = 0;
    CHECK("find numeric export", dao_module_find_export(module, 100, &function) == DAO_OK);
    CHECK("missing export", dao_module_find_export(module, 999, &function) == DAO_EXPORT_NOT_FOUND);

    const dao_value args[] = {
        {DAO_VALUE_I64, 0, 40},
        {DAO_VALUE_I64, 0, 2},
    };
    dao_value result{};
    CHECK("call add", dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_OK);
    CHECK("add result type", result.type == DAO_VALUE_I64);
    CHECK("add result value", result.payload == 42);
    CHECK("module fingerprint", dao_module_fingerprint(module) != 0);
    dao_module_release(module);
}

void test_internal_call(dao_vm* vm) {
    dao::ModuleBuilder builder;

    dao::FunctionSpec add;
    add.parameter_count = 2;
    add.register_count = 3;
    add.code = {
        instruction(dao::Opcode::AddI64, 2, 0, 1),
        instruction(dao::Opcode::Return, 0, 2),
    };
    builder.add_function(std::move(add));

    dao::FunctionSpec wrapper;
    wrapper.parameter_count = 2;
    wrapper.register_count = 3;
    wrapper.code = {
        instruction(dao::Opcode::Call, 2, 0, 2, 0),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t wrapper_index = builder.add_function(std::move(wrapper));
    builder.add_export(200, wrapper_index);

    const auto bytes = builder.encode();
    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load call module", module != nullptr);
    if (module == nullptr)
        return;

    dao_function function = 0;
    CHECK("find call export", dao_module_find_export(module, 200, &function) == DAO_OK);
    const dao_value args[] = {{DAO_VALUE_I64, 0, 19}, {DAO_VALUE_I64, 0, 23}};
    dao_value result{};
    CHECK("internal call", dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_OK);
    CHECK("internal call result", result.type == DAO_VALUE_I64 && result.payload == 42);
    dao_module_release(module);
}

void test_host_imports(dao_vm* vm) {
    dao::ModuleBuilder builder;
    const uint32_t import_index = builder.add_import(700, 2);
    dao::FunctionSpec wrapper;
    wrapper.parameter_count = 2;
    wrapper.register_count = 3;
    wrapper.code = {
        instruction(dao::Opcode::CallHost, 2, 0, 2, import_index),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t function_index = builder.add_function(std::move(wrapper));
    builder.add_export(701, function_index);

    const auto bytes = builder.encode();
    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load host import module", module != nullptr);
    if (module == nullptr)
        return;

    dao_function function = 0;
    CHECK("find host wrapper", dao_module_find_export(module, 701, &function) == DAO_OK);
    const dao_value args[] = {{DAO_VALUE_I64, 0, 20}, {DAO_VALUE_I64, 0, 21}};
    dao_value result{};
    CHECK("missing host import",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_IMPORT_NOT_FOUND);

    int64_t bias = 1;
    dao_host_function host{sizeof(dao_host_function), 700, 2, 0, host_add_bias, &bias};
    CHECK("register host import", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("reject duplicate host registration",
          dao_vm_register_host_function(vm, &host) == DAO_INVALID_ARGUMENT);
    CHECK("call host import",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_OK);
    CHECK("host import result", result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("unregister host import", dao_vm_unregister_host_function(vm, 700) == DAO_OK);

    host.parameter_count = 1;
    CHECK("register mismatched host", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("reject host signature mismatch",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_TYPE_ERROR);
    CHECK("unregister mismatched host", dao_vm_unregister_host_function(vm, 700) == DAO_OK);

    host.parameter_count = 2;
    host.callback = host_invalid_value;
    CHECK("register invalid-value host", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("reject invalid host value",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_TYPE_ERROR);
    CHECK("unregister invalid-value host", dao_vm_unregister_host_function(vm, 700) == DAO_OK);

    host.callback = host_error;
    CHECK("register error host", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("propagate host status",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_DIVIDE_BY_ZERO);
    CHECK("host error location", error.function_index == function && error.instruction_index == 0);
    CHECK("unregister error host", dao_vm_unregister_host_function(vm, 700) == DAO_OK);

    host.callback = host_throws;
    CHECK("register throwing host", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("contain host exception",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_RUNTIME_ERROR);
    CHECK("unregister throwing host", dao_vm_unregister_host_function(vm, 700) == DAO_OK);
    CHECK("missing host unregister",
          dao_vm_unregister_host_function(vm, 700) == DAO_IMPORT_NOT_FOUND);

    dao_module_release(module);
}

void test_trit_logic_and_branch(dao_vm* vm) {
    dao::ModuleBuilder builder;

    dao::FunctionSpec trit_and;
    trit_and.parameter_count = 2;
    trit_and.register_count = 3;
    trit_and.code = {
        instruction(dao::Opcode::TritAnd, 2, 0, 1),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t and_index = builder.add_function(std::move(trit_and));
    builder.add_export(300, and_index);

    dao::FunctionSpec zero_branch;
    zero_branch.parameter_count = 1;
    zero_branch.register_count = 2;
    zero_branch.code = {
        instruction(dao::Opcode::BranchTritZero, 0, 0, 0, 3),
        instruction(dao::Opcode::LoadI64, 1, 0, 0, 1),
        instruction(dao::Opcode::Return, 0, 1),
        instruction(dao::Opcode::LoadI64, 1, 0, 0, 0),
        instruction(dao::Opcode::Return, 0, 1),
    };
    const uint32_t branch_index = builder.add_function(std::move(zero_branch));
    builder.add_export(301, branch_index);

    const auto bytes = builder.encode();
    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load trit module", module != nullptr);
    if (module == nullptr)
        return;

    dao_function function = 0;
    CHECK("find trit export", dao_module_find_export(module, 300, &function) == DAO_OK);
    const dao_value trits[] = {{DAO_VALUE_TRIT, 0, 0}, {DAO_VALUE_TRIT, 0, 1}};
    dao_value result{};
    CHECK("trit and", dao_vm_call(vm, module, function, trits, 2, &result, &error) == DAO_OK);
    CHECK("trit unknown preserved", result.type == DAO_VALUE_TRIT && result.payload == 0);

    CHECK("find branch export", dao_module_find_export(module, 301, &function) == DAO_OK);
    CHECK("zero branch", dao_vm_call(vm, module, function, trits, 1, &result, &error) == DAO_OK);
    CHECK("zero branch result", result.type == DAO_VALUE_I64 && result.payload == 0);
    const dao_value positive[] = {{DAO_VALUE_TRIT, 0, 1}};
    CHECK("positive branch",
          dao_vm_call(vm, module, function, positive, 1, &result, &error) == DAO_OK);
    CHECK("positive branch result", result.type == DAO_VALUE_I64 && result.payload == 1);
    dao_module_release(module);
}

void test_bad_modules(dao_vm* vm) {
    dao_error error{};
    auto bad_magic = make_add_module(100);
    bad_magic[0] = 'X';
    dao_module* module = nullptr;
    CHECK("reject bad magic", dao_vm_load_module(vm, dao_bytes{bad_magic.data(), bad_magic.size()},
                                                 &module, &error) == DAO_BAD_MODULE);

    auto bad_version = make_add_module(100);
    bad_version[4] = 2;
    CHECK("reject bad version",
          dao_vm_load_module(vm, dao_bytes{bad_version.data(), bad_version.size()}, &module,
                             &error) == DAO_BAD_MODULE);

    auto overlapping_sections = make_add_module(100);
    const uint32_t first_section_offset = static_cast<uint32_t>(overlapping_sections[20]) |
                                          (static_cast<uint32_t>(overlapping_sections[21]) << 8) |
                                          (static_cast<uint32_t>(overlapping_sections[22]) << 16) |
                                          (static_cast<uint32_t>(overlapping_sections[23]) << 24);
    write_u32(overlapping_sections, 36, first_section_offset);
    CHECK("reject overlapping sections",
          dao_vm_load_module(vm,
                             dao_bytes{overlapping_sections.data(), overlapping_sections.size()},
                             &module, &error) == DAO_BAD_MODULE);

    dao::ModuleBuilder builder;
    dao::FunctionSpec invalid;
    invalid.parameter_count = 0;
    invalid.register_count = 1;
    invalid.code = {
        instruction(dao::Opcode::LoadI64, 7, 0, 0, 42),
        instruction(dao::Opcode::Return, 0, 0),
    };
    const uint32_t index = builder.add_function(std::move(invalid));
    builder.add_export(400, index);
    const auto bad_register = builder.encode();
    CHECK("reject bad register",
          dao_vm_load_module(vm, dao_bytes{bad_register.data(), bad_register.size()}, &module,
                             &error) == DAO_VERIFY_ERROR);

    auto nonzero_function_reserved = make_add_module(100);
    const size_t function_offset = read_u32(nonzero_function_reserved, 20);
    write_u32(nonzero_function_reserved, function_offset + 12, 1);
    CHECK("reject nonzero function reserved field",
          dao_vm_load_module(
              vm, dao_bytes{nonzero_function_reserved.data(), nonzero_function_reserved.size()},
              &module, &error) == DAO_VERIFY_ERROR);

    auto nonzero_instruction_flags = make_add_module(100);
    const size_t code_offset = read_u32(nonzero_instruction_flags, 36);
    nonzero_instruction_flags[code_offset + 1] = 1;
    CHECK("reject nonzero instruction flags",
          dao_vm_load_module(
              vm, dao_bytes{nonzero_instruction_flags.data(), nonzero_instruction_flags.size()},
              &module, &error) == DAO_VERIFY_ERROR);

    dao::ModuleBuilder bad_host_call_builder;
    dao::FunctionSpec bad_host_call;
    bad_host_call.register_count = 1;
    bad_host_call.code = {
        instruction(dao::Opcode::CallHost, 0, 0, 0, 0),
        instruction(dao::Opcode::Return, 0, 0),
    };
    const uint32_t bad_host_function = bad_host_call_builder.add_function(std::move(bad_host_call));
    bad_host_call_builder.add_export(401, bad_host_function);
    const auto missing_import = bad_host_call_builder.encode();
    CHECK("reject host call without import",
          dao_vm_load_module(vm, dao_bytes{missing_import.data(), missing_import.size()}, &module,
                             &error) == DAO_VERIFY_ERROR);

    dao::ModuleBuilder bad_import_builder;
    bad_import_builder.add_import(800, 0);
    dao::FunctionSpec import_function;
    import_function.register_count = 1;
    import_function.code = {
        instruction(dao::Opcode::LoadI64, 0, 0, 0, 1),
        instruction(dao::Opcode::Return, 0, 0),
    };
    const uint32_t import_function_index =
        bad_import_builder.add_function(std::move(import_function));
    bad_import_builder.add_export(402, import_function_index);
    auto bad_import_reserved = bad_import_builder.encode();
    const size_t import_offset = read_u32(bad_import_reserved, 68);
    bad_import_reserved[import_offset + 6] = 1;
    CHECK("reject nonzero import reserved field",
          dao_vm_load_module(vm, dao_bytes{bad_import_reserved.data(), bad_import_reserved.size()},
                             &module, &error) == DAO_VERIFY_ERROR);
}

void test_runtime_errors(dao_vm* vm) {
    dao::ModuleBuilder builder;
    dao::FunctionSpec divide;
    divide.parameter_count = 2;
    divide.register_count = 3;
    divide.code = {
        instruction(dao::Opcode::DivI64, 2, 0, 1),
        instruction(dao::Opcode::Return, 0, 2),
    };
    const uint32_t divide_index = builder.add_function(std::move(divide));
    builder.add_export(500, divide_index);
    const auto bytes = builder.encode();

    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load divide module", module != nullptr);
    if (module == nullptr)
        return;
    dao_function function = 0;
    CHECK("find divide export", dao_module_find_export(module, 500, &function) == DAO_OK);
    const dao_value args[] = {{DAO_VALUE_I64, 0, 1}, {DAO_VALUE_I64, 0, 0}};
    dao_value result{};
    CHECK("division by zero",
          dao_vm_call(vm, module, function, args, 2, &result, &error) == DAO_DIVIDE_BY_ZERO);
    CHECK("division error location", error.instruction_index == 0);
    dao_module_release(module);
}

void test_instruction_budget() {
    dao_vm_config config = dao_vm_config_default();
    config.max_instructions_per_call = 8;
    dao_vm* vm = dao_vm_create(&config);
    CHECK("create budget vm", vm != nullptr);
    if (vm == nullptr)
        return;

    dao::ModuleBuilder builder;
    dao::FunctionSpec loop;
    loop.parameter_count = 0;
    loop.register_count = 0;
    loop.code = {instruction(dao::Opcode::Jump, 0, 0, 0, 0)};
    const uint32_t index = builder.add_function(std::move(loop));
    builder.add_export(600, index);
    const auto bytes = builder.encode();

    dao_error error{};
    dao_module* module = load_module(vm, bytes, &error);
    CHECK("load loop module", module != nullptr);
    if (module != nullptr) {
        dao_function function = 0;
        dao_value result{};
        CHECK("find loop export", dao_module_find_export(module, 600, &function) == DAO_OK);
        CHECK("instruction budget", dao_vm_call(vm, module, function, nullptr, 0, &result,
                                                &error) == DAO_INSTRUCTION_LIMIT_EXCEEDED);
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
}

} // namespace

int main() {
    dao_vm* vm = dao_vm_create(nullptr);
    CHECK("create default vm", vm != nullptr);
    if (vm == nullptr)
        return EXIT_FAILURE;

    test_deterministic_encoding();
    test_add_and_export(vm);
    test_internal_call(vm);
    test_host_imports(vm);
    test_trit_logic_and_branch(vm);
    test_bad_modules(vm);
    test_runtime_errors(vm);
    dao_vm_destroy(vm);
    test_instruction_budget();

    if (failures != 0) {
        std::cerr << failures << " kernel test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "dao kernel tests passed\n";
    return EXIT_SUCCESS;
}
