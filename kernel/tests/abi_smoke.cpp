#include "dao/dao.h"
#include "dao/format.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    dao::ModuleBuilder builder;
    dao::FunctionSpec function;
    function.parameter_count = 0;
    function.register_count = 1;
    function.code = {
        {dao::Opcode::LoadI64, 0, 0, 0, 0, 42},
        {dao::Opcode::Return, 0, 0, 0, 0, 0},
    };
    const uint32_t function_index = builder.add_function(std::move(function));
    builder.add_export(1, function_index);
    const auto bytes = builder.encode();

    dao_vm* vm = dao_vm_create(nullptr);
    if (vm == nullptr)
        return EXIT_FAILURE;

    dao_error error{};
    dao_module* module = nullptr;
    if (dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) != DAO_OK) {
        dao_vm_destroy(vm);
        return EXIT_FAILURE;
    }

    dao_function exported = 0;
    dao_value result{};
    const bool ok = dao_module_find_export(module, 1, &exported) == DAO_OK &&
                    dao_vm_call(vm, module, exported, nullptr, 0, &result, &error) == DAO_OK &&
                    result.type == DAO_VALUE_I64 && result.payload == 42;

    dao_module_release(module);
    dao_vm_destroy(vm);
    if (!ok)
        return EXIT_FAILURE;
    std::cout << "dao C ABI smoke passed\n";
    return EXIT_SUCCESS;
}
