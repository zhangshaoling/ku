#ifndef DAO_KERNEL_DISASSEMBLE_HPP
#define DAO_KERNEL_DISASSEMBLE_HPP

#include "dao/dao.h"
#include "dao/format.hpp"

#include <string>
#include <vector>

namespace dao {

struct DisassembledFunction {
    uint32_t index;
    uint16_t register_count;
    uint16_t parameter_count;
    struct Instruction {
        uint32_t address;
        Opcode opcode;
        uint16_t dst;
        uint16_t a;
        uint16_t b;
        int64_t immediate;
    };
    std::vector<Instruction> instructions;
};

struct DisassembledImport {
    uint32_t symbol_id;
    uint16_t parameter_count;
};

struct DisassembledExport {
    uint32_t symbol_id;
    uint32_t function_index;
};

struct DisassembledModule {
    std::vector<DisassembledFunction> functions;
    std::vector<DisassembledImport> imports;
    std::vector<DisassembledExport> exports;
};

dao_status disassemble(dao_bytes bytes, DisassembledModule* out_module, dao_error* error);

std::string to_text(const DisassembledModule& module);

} // namespace dao

#endif
