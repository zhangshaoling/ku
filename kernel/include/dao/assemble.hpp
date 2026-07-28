#ifndef DAO_KERNEL_ASSEMBLE_HPP
#define DAO_KERNEL_ASSEMBLE_HPP

#include "dao/disassemble.hpp"
#include "dao/format.hpp"

#include <string>
#include <vector>

namespace dao {

struct ParsedImport {
    uint32_t symbol_id = 0;
    uint16_t parameter_count = 0;
};

struct ParsedFunction {
    uint16_t register_count = 0;
    uint16_t parameter_count = 0;
    std::vector<Instruction> code;
};

struct ParsedExport {
    uint32_t symbol_id = 0;
    uint32_t function_index = 0;
};

struct ParsedModuleImport {
    std::string module_name;
    SemanticVersion version{};
    uint32_t symbol_id = 0;
    uint16_t parameter_count = 0;
};

struct ParsedModule {
    bool has_identity = false;
    std::string identity_name;
    SemanticVersion identity_version{};
    std::vector<std::string> strings;
    std::vector<ParsedImport> imports;
    std::vector<ParsedFunction> functions;
    std::vector<ParsedExport> exports;
    std::vector<ParsedModuleImport> module_imports;
};

bool parse_module_text(const std::string& text, ParsedModule* out_module, dao_error* error);

bool assemble_text(const std::string& text, std::vector<uint8_t>* out_bytes, dao_error* error);

bool assemble_to_builder(const std::string& text, ModuleBuilder* builder, dao_error* error);

} // namespace dao

#endif
