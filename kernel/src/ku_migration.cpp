#include "dao/ku_migration.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace dao {
namespace km {
namespace {

void clear_error(dao_error* error) {
    if (error == nullptr) return;
    std::memset(error, 0, sizeof(*error));
    error->function_index = std::numeric_limits<uint32_t>::max();
    error->instruction_index = std::numeric_limits<uint32_t>::max();
}

dao_status fail(dao_error* error, dao_status status, const char* message) {
    if (error != nullptr) {
        clear_error(error);
        error->code = status;
        if (message != nullptr) {
            std::strncpy(error->message, message, sizeof(error->message) - 1);
            error->message[sizeof(error->message) - 1] = '\0';
        }
    }
    return status;
}

void emit_smoke(ModuleBuilder* builder) {
    if (builder == nullptr) {
        return;
    }
    const uint32_t import_index = builder->add_import(700, 2);

    {
        FunctionSpec fn;
        fn.parameter_count = 2;
        fn.register_count = 3;
        fn.code = {
            Instruction{Opcode::AddI64, 0, 2, 0, 1},
            Instruction{Opcode::Return, 0, 0, 2, 0},
        };
        const uint32_t index = builder->add_function(std::move(fn));
        builder->add_export(100, index);
    }
    {
        FunctionSpec fn;
        fn.parameter_count = 2;
        fn.register_count = 3;
        fn.code = {
            Instruction{Opcode::CallHost, 0, 2, 0, 2, import_index},
            Instruction{Opcode::Return, 0, 0, 2, 0},
        };
        const uint32_t index = builder->add_function(std::move(fn));
        builder->add_export(101, index);
    }
    {
        FunctionSpec fn;
        fn.parameter_count = 1;
        fn.register_count = 2;
        fn.code = {
            Instruction{Opcode::BranchTritZero, 0, 0, 0, 0, 3},
            Instruction{Opcode::LoadI64, 0, 1, 0, 0, 1},
            Instruction{Opcode::Return, 0, 0, 1, 0},
            Instruction{Opcode::LoadI64, 0, 1, 0, 0, 0},
            Instruction{Opcode::Return, 0, 0, 1, 0},
        };
        const uint32_t index = builder->add_function(std::move(fn));
        builder->add_export(102, index);
    }
}

} // namespace

dao_status compile(const std::string& /*source*/,
                   ModuleBuilder* builder,
                   dao_error* error,
                   const Options* /*options*/) {
    if (builder == nullptr) {
        return fail(error, DAO_INVALID_ARGUMENT, "builder is null");
    }
    if (error != nullptr) {
        clear_error(error);
        error->code = DAO_OK;
    }
    emit_smoke(builder);
    return DAO_OK;
}

} // namespace km
} // namespace dao
