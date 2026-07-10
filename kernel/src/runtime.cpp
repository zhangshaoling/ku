#include "dao/dao.h"
#include "dao/format.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct FunctionRecord {
    uint32_t code_offset;
    uint32_t code_count;
    uint16_t register_count;
    uint16_t parameter_count;
};

struct SectionRecord {
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t count;
};

struct ImportRecord {
    uint32_t symbol_id;
    uint16_t parameter_count;
};

struct HostFunction {
    uint32_t parameter_count;
    dao_host_callback callback;
    void* user_data;
};

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_u32(const uint8_t* data) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(*data++) << shift;
    }
    return value;
}

int64_t read_i64(const uint8_t* data) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(*data++) << shift;
    }
    return static_cast<int64_t>(value);
}

uint64_t fingerprint64(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void clear_error(dao_error* error) {
    if (error == nullptr)
        return;
    std::memset(error, 0, sizeof(*error));
    error->function_index = std::numeric_limits<uint32_t>::max();
    error->instruction_index = std::numeric_limits<uint32_t>::max();
}

dao_status fail(dao_error* error, dao_status status, const char* message,
                uint32_t function_index = std::numeric_limits<uint32_t>::max(),
                uint32_t instruction_index = std::numeric_limits<uint32_t>::max()) {
    if (error != nullptr) {
        clear_error(error);
        error->code = status;
        error->function_index = function_index;
        error->instruction_index = instruction_index;
        if (message != nullptr) {
            std::strncpy(error->message, message, sizeof(error->message) - 1);
            error->message[sizeof(error->message) - 1] = '\0';
        }
    }
    return status;
}

bool checked_add(int64_t left, int64_t right, int64_t& out) {
    if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int64_t>::min() - right)) {
        return false;
    }
    out = left + right;
    return true;
}

bool checked_sub(int64_t left, int64_t right, int64_t& out) {
    if ((right < 0 && left > std::numeric_limits<int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<int64_t>::min() + right)) {
        return false;
    }
    out = left - right;
    return true;
}

bool checked_mul(int64_t left, int64_t right, int64_t& out) {
    if (left == 0 || right == 0) {
        out = 0;
        return true;
    }
    if ((left == -1 && right == std::numeric_limits<int64_t>::min()) ||
        (right == -1 && left == std::numeric_limits<int64_t>::min())) {
        return false;
    }
    if (left > 0) {
        if (right > 0 && left > std::numeric_limits<int64_t>::max() / right)
            return false;
        if (right < 0 && right < std::numeric_limits<int64_t>::min() / left)
            return false;
    } else {
        if (right > 0 && left < std::numeric_limits<int64_t>::min() / right)
            return false;
        if (right < 0 && left < std::numeric_limits<int64_t>::max() / right)
            return false;
    }
    out = left * right;
    return true;
}

dao_value null_value() { return dao_value{DAO_VALUE_NULL, 0, 0}; }

bool is_trit(const dao_value& value) {
    return value.type == DAO_VALUE_TRIT && value.payload >= -1 && value.payload <= 1;
}

bool is_valid_value(const dao_value& value) {
    if (value.reserved != 0)
        return false;
    if (value.type == DAO_VALUE_NULL)
        return value.payload == 0;
    if (value.type == DAO_VALUE_I64)
        return true;
    return is_trit(value);
}

} // namespace

struct dao_vm {
    dao_vm_config config;
    std::unordered_map<uint32_t, HostFunction> host_functions;
};

struct dao_module {
    std::vector<FunctionRecord> functions;
    std::vector<dao::Instruction> code;
    std::vector<ImportRecord> imports;
    std::unordered_map<uint32_t, uint32_t> exports;
    uint64_t fingerprint = 0;
};

namespace {

const SectionRecord* find_section(const std::vector<SectionRecord>& sections,
                                  dao::SectionType type) {
    const uint32_t wanted = static_cast<uint32_t>(type);
    const auto found =
        std::find_if(sections.begin(), sections.end(),
                     [wanted](const SectionRecord& section) { return section.type == wanted; });
    return found == sections.end() ? nullptr : &*found;
}

dao_status verify_instruction(const dao_module& module, const FunctionRecord& function,
                              const dao::Instruction& instruction, uint32_t function_index,
                              uint32_t pc, dao_error* error) {
    if (instruction.flags != 0) {
        return fail(error, DAO_VERIFY_ERROR, "instruction flags must be zero in VM ABI v2",
                    function_index, pc);
    }

    const auto valid_register = [&function](uint16_t index) {
        return index < function.register_count;
    };
    const auto valid_target = [&function](int64_t target) {
        return target >= 0 && static_cast<uint64_t>(target) < function.code_count;
    };

    using dao::Opcode;
    switch (instruction.opcode) {
    case Opcode::Nop:
        return DAO_OK;
    case Opcode::LoadI64:
        if (valid_register(instruction.dst))
            return DAO_OK;
        break;
    case Opcode::Move:
    case Opcode::TritNot:
        if (valid_register(instruction.dst) && valid_register(instruction.a))
            return DAO_OK;
        break;
    case Opcode::AddI64:
    case Opcode::SubI64:
    case Opcode::MulI64:
    case Opcode::DivI64:
    case Opcode::TritAnd:
    case Opcode::TritOr:
        if (valid_register(instruction.dst) && valid_register(instruction.a) &&
            valid_register(instruction.b)) {
            return DAO_OK;
        }
        break;
    case Opcode::BranchTritNegative:
    case Opcode::BranchTritZero:
    case Opcode::BranchTritPositive:
        if (valid_register(instruction.a) && valid_target(instruction.immediate))
            return DAO_OK;
        break;
    case Opcode::Jump:
        if (valid_target(instruction.immediate))
            return DAO_OK;
        break;
    case Opcode::Call: {
        if (!valid_register(instruction.dst) || instruction.immediate < 0 ||
            static_cast<uint64_t>(instruction.immediate) >= module.functions.size()) {
            break;
        }
        const uint32_t end =
            static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b);
        const auto& callee = module.functions[static_cast<size_t>(instruction.immediate)];
        if (end <= function.register_count && instruction.b == callee.parameter_count)
            return DAO_OK;
        break;
    }
    case Opcode::CallHost: {
        if (!valid_register(instruction.dst) || instruction.immediate < 0 ||
            static_cast<uint64_t>(instruction.immediate) >= module.imports.size()) {
            break;
        }
        const uint32_t end =
            static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b);
        const auto& import = module.imports[static_cast<size_t>(instruction.immediate)];
        if (end <= function.register_count && instruction.b == import.parameter_count)
            return DAO_OK;
        break;
    }
    case Opcode::Return:
        if (valid_register(instruction.a))
            return DAO_OK;
        break;
    default:
        return fail(error, DAO_VERIFY_ERROR, "unknown opcode", function_index, pc);
    }
    return fail(error, DAO_VERIFY_ERROR, "invalid instruction operand", function_index, pc);
}

dao_status execute_function(dao_vm* vm, const dao_module* module, uint32_t function_index,
                            const dao_value* args, size_t arg_count, uint32_t depth,
                            uint64_t& budget, dao_value* out, dao_error* error) {
    if (depth >= vm->config.max_call_depth) {
        return fail(error, DAO_CALL_DEPTH_EXCEEDED, "maximum call depth exceeded", function_index);
    }
    if (function_index >= module->functions.size()) {
        return fail(error, DAO_RUNTIME_ERROR, "function index out of range", function_index);
    }

    const auto& function = module->functions[function_index];
    if (arg_count != function.parameter_count) {
        return fail(error, DAO_INVALID_ARGUMENT, "argument count does not match function signature",
                    function_index);
    }

    std::vector<dao_value> registers(function.register_count, null_value());
    for (size_t index = 0; index < arg_count; ++index)
        registers[index] = args[index];

    uint32_t pc = 0;
    while (pc < function.code_count) {
        if (budget == 0) {
            return fail(error, DAO_INSTRUCTION_LIMIT_EXCEEDED, "instruction budget exhausted",
                        function_index, pc);
        }
        --budget;

        const auto& instruction = module->code[function.code_offset + pc];
        const auto require_i64 = [&](uint16_t index) {
            return registers[index].type == DAO_VALUE_I64;
        };

        using dao::Opcode;
        switch (instruction.opcode) {
        case Opcode::Nop:
            ++pc;
            break;
        case Opcode::LoadI64:
            registers[instruction.dst] = dao_value{DAO_VALUE_I64, 0, instruction.immediate};
            ++pc;
            break;
        case Opcode::Move:
            registers[instruction.dst] = registers[instruction.a];
            ++pc;
            break;
        case Opcode::AddI64:
        case Opcode::SubI64:
        case Opcode::MulI64:
        case Opcode::DivI64: {
            if (!require_i64(instruction.a) || !require_i64(instruction.b)) {
                return fail(error, DAO_TYPE_ERROR, "integer opcode requires i64 operands",
                            function_index, pc);
            }
            const int64_t left = registers[instruction.a].payload;
            const int64_t right = registers[instruction.b].payload;
            int64_t result = 0;
            bool valid = true;
            if (instruction.opcode == Opcode::AddI64)
                valid = checked_add(left, right, result);
            if (instruction.opcode == Opcode::SubI64)
                valid = checked_sub(left, right, result);
            if (instruction.opcode == Opcode::MulI64)
                valid = checked_mul(left, right, result);
            if (instruction.opcode == Opcode::DivI64) {
                if (right == 0) {
                    return fail(error, DAO_DIVIDE_BY_ZERO, "integer division by zero",
                                function_index, pc);
                }
                if (left == std::numeric_limits<int64_t>::min() && right == -1)
                    valid = false;
                else
                    result = left / right;
            }
            if (!valid) {
                return fail(error, DAO_INTEGER_OVERFLOW, "integer overflow", function_index, pc);
            }
            registers[instruction.dst] = dao_value{DAO_VALUE_I64, 0, result};
            ++pc;
            break;
        }
        case Opcode::TritNot:
            if (!is_trit(registers[instruction.a])) {
                return fail(error, DAO_TYPE_ERROR, "TRIT_NOT requires a trit", function_index, pc);
            }
            registers[instruction.dst] =
                dao_value{DAO_VALUE_TRIT, 0, -registers[instruction.a].payload};
            ++pc;
            break;
        case Opcode::TritAnd:
        case Opcode::TritOr:
            if (!is_trit(registers[instruction.a]) || !is_trit(registers[instruction.b])) {
                return fail(error, DAO_TYPE_ERROR, "trit opcode requires trit operands",
                            function_index, pc);
            }
            registers[instruction.dst] = dao_value{
                DAO_VALUE_TRIT, 0,
                instruction.opcode == Opcode::TritAnd
                    ? std::min(registers[instruction.a].payload, registers[instruction.b].payload)
                    : std::max(registers[instruction.a].payload, registers[instruction.b].payload)};
            ++pc;
            break;
        case Opcode::BranchTritNegative:
        case Opcode::BranchTritZero:
        case Opcode::BranchTritPositive: {
            if (!is_trit(registers[instruction.a])) {
                return fail(error, DAO_TYPE_ERROR, "trit branch requires a trit", function_index,
                            pc);
            }
            const int64_t value = registers[instruction.a].payload;
            const bool take = (instruction.opcode == Opcode::BranchTritNegative && value < 0) ||
                              (instruction.opcode == Opcode::BranchTritZero && value == 0) ||
                              (instruction.opcode == Opcode::BranchTritPositive && value > 0);
            pc = take ? static_cast<uint32_t>(instruction.immediate) : pc + 1;
            break;
        }
        case Opcode::Jump:
            pc = static_cast<uint32_t>(instruction.immediate);
            break;
        case Opcode::Call: {
            dao_value result = null_value();
            const dao_status status =
                execute_function(vm, module, static_cast<uint32_t>(instruction.immediate),
                                 instruction.b == 0 ? nullptr : registers.data() + instruction.a,
                                 instruction.b, depth + 1, budget, &result, error);
            if (status != DAO_OK)
                return status;
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::CallHost: {
            const auto& import = module->imports[static_cast<size_t>(instruction.immediate)];
            const auto found = vm->host_functions.find(import.symbol_id);
            if (found == vm->host_functions.end()) {
                return fail(error, DAO_IMPORT_NOT_FOUND, "host import is not registered",
                            function_index, pc);
            }
            const HostFunction& host = found->second;
            if (host.parameter_count != import.parameter_count) {
                return fail(error, DAO_TYPE_ERROR, "host import signature does not match module",
                            function_index, pc);
            }

            dao_value result = null_value();
            dao_status status = DAO_RUNTIME_ERROR;
            try {
                status = host.callback(
                    host.user_data, instruction.b == 0 ? nullptr : registers.data() + instruction.a,
                    instruction.b, &result);
            } catch (...) {
                return fail(error, DAO_RUNTIME_ERROR, "host callback threw an exception",
                            function_index, pc);
            }
            if (status != DAO_OK) {
                if (status < DAO_INVALID_ARGUMENT || status > DAO_IMPORT_NOT_FOUND)
                    status = DAO_RUNTIME_ERROR;
                return fail(error, status, "host callback returned an error", function_index, pc);
            }
            if (!is_valid_value(result)) {
                return fail(error, DAO_TYPE_ERROR, "host callback returned an invalid value",
                            function_index, pc);
            }
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::Return:
            *out = registers[instruction.a];
            return DAO_OK;
        default:
            return fail(error, DAO_RUNTIME_ERROR, "unknown opcode reached execution",
                        function_index, pc);
        }
    }

    return fail(error, DAO_RUNTIME_ERROR, "function ended without RETURN", function_index, pc);
}

} // namespace

extern "C" {

dao_vm_config dao_vm_config_default(void) {
    dao_vm_config config{};
    config.struct_size = sizeof(dao_vm_config);
    config.max_registers = 4096;
    config.max_call_depth = 1024;
    config.max_module_bytes = 64ULL * 1024ULL * 1024ULL;
    config.max_instructions_per_call = 10ULL * 1000ULL * 1000ULL;
    return config;
}

dao_vm* dao_vm_create(const dao_vm_config* requested) {
    dao_vm_config config = dao_vm_config_default();
    if (requested != nullptr) {
        if (requested->struct_size != sizeof(dao_vm_config))
            return nullptr;
        config = *requested;
    }
    if (config.max_registers == 0 || config.max_call_depth == 0 || config.max_module_bytes == 0 ||
        config.max_instructions_per_call == 0) {
        return nullptr;
    }
    return new (std::nothrow) dao_vm{config, {}};
}

void dao_vm_destroy(dao_vm* vm) { delete vm; }

dao_status dao_vm_register_host_function(dao_vm* vm, const dao_host_function* function) {
    if (vm == nullptr || function == nullptr || function->struct_size != sizeof(*function) ||
        function->reserved != 0 || function->callback == nullptr ||
        function->parameter_count > std::numeric_limits<uint16_t>::max() ||
        function->parameter_count > vm->config.max_registers) {
        return DAO_INVALID_ARGUMENT;
    }
    try {
        const auto [unused, inserted] = vm->host_functions.emplace(
            function->symbol_id,
            HostFunction{function->parameter_count, function->callback, function->user_data});
        (void)unused;
        return inserted ? DAO_OK : DAO_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_vm_unregister_host_function(dao_vm* vm, uint32_t symbol_id) {
    if (vm == nullptr)
        return DAO_INVALID_ARGUMENT;
    return vm->host_functions.erase(symbol_id) == 1 ? DAO_OK : DAO_IMPORT_NOT_FOUND;
}

dao_status dao_vm_load_module(dao_vm* vm, dao_bytes bytes, dao_module** out_module,
                              dao_error* error) {
    clear_error(error);
    if (vm == nullptr || out_module == nullptr || bytes.data == nullptr) {
        return fail(error, DAO_INVALID_ARGUMENT, "vm, bytes, and out_module are required");
    }
    *out_module = nullptr;
    if (bytes.size < dao::kHeaderSize || bytes.size > vm->config.max_module_bytes) {
        return fail(error, DAO_BAD_MODULE, "module size is invalid");
    }
    if (std::memcmp(bytes.data, "DAO\0", 4) != 0) {
        return fail(error, DAO_BAD_MODULE, "invalid module magic");
    }
    if (read_u16(bytes.data + 4) != dao::kFormatVersion ||
        read_u16(bytes.data + 6) != dao::kVmAbiVersion) {
        return fail(error, DAO_BAD_MODULE, "unsupported format or VM ABI version");
    }
    if (read_u32(bytes.data + 8) != 0) {
        return fail(error, DAO_BAD_MODULE, "unsupported module flags");
    }

    const uint32_t section_count = read_u32(bytes.data + 12);
    if (section_count == 0 || section_count > 32) {
        return fail(error, DAO_BAD_MODULE, "invalid section count");
    }
    const uint64_t table_end = static_cast<uint64_t>(dao::kHeaderSize) +
                               static_cast<uint64_t>(section_count) * dao::kSectionEntrySize;
    if (table_end > bytes.size)
        return fail(error, DAO_BAD_MODULE, "truncated section table");

    std::vector<SectionRecord> sections;
    sections.reserve(section_count);
    for (uint32_t index = 0; index < section_count; ++index) {
        const uint8_t* entry =
            bytes.data + dao::kHeaderSize + static_cast<size_t>(index) * dao::kSectionEntrySize;
        SectionRecord section{read_u32(entry), read_u32(entry + 4), read_u32(entry + 8),
                              read_u32(entry + 12)};
        if (section.type < static_cast<uint32_t>(dao::SectionType::Functions) ||
            section.type > static_cast<uint32_t>(dao::SectionType::Imports)) {
            return fail(error, DAO_BAD_MODULE, "unknown section type");
        }
        const uint64_t end = static_cast<uint64_t>(section.offset) + section.size;
        if (section.offset < table_end || end > bytes.size) {
            return fail(error, DAO_BAD_MODULE, "section lies outside module bounds");
        }
        if (find_section(sections, static_cast<dao::SectionType>(section.type)) != nullptr) {
            return fail(error, DAO_BAD_MODULE, "duplicate section type");
        }
        sections.push_back(section);
    }

    auto ranges = sections;
    std::sort(ranges.begin(), ranges.end(),
              [](const SectionRecord& left, const SectionRecord& right) {
                  return left.offset < right.offset;
              });
    for (size_t index = 1; index < ranges.size(); ++index) {
        const uint64_t previous_end =
            static_cast<uint64_t>(ranges[index - 1].offset) + ranges[index - 1].size;
        if (ranges[index - 1].size != 0 && ranges[index].size != 0 &&
            previous_end > ranges[index].offset) {
            return fail(error, DAO_BAD_MODULE, "module sections overlap");
        }
    }

    const SectionRecord* functions_section = find_section(sections, dao::SectionType::Functions);
    const SectionRecord* code_section = find_section(sections, dao::SectionType::Code);
    const SectionRecord* exports_section = find_section(sections, dao::SectionType::Exports);
    const SectionRecord* imports_section = find_section(sections, dao::SectionType::Imports);
    if (functions_section == nullptr || code_section == nullptr || exports_section == nullptr ||
        imports_section == nullptr) {
        return fail(error, DAO_BAD_MODULE, "required section is missing");
    }
    if (functions_section->size !=
            static_cast<uint64_t>(functions_section->count) * dao::kFunctionRecordSize ||
        code_section->size != static_cast<uint64_t>(code_section->count) * dao::kInstructionSize ||
        exports_section->size !=
            static_cast<uint64_t>(exports_section->count) * dao::kExportRecordSize ||
        imports_section->size !=
            static_cast<uint64_t>(imports_section->count) * dao::kImportRecordSize) {
        return fail(error, DAO_BAD_MODULE, "section size does not match record count");
    }

    dao_module* module = new (std::nothrow) dao_module();
    if (module == nullptr)
        return fail(error, DAO_OUT_OF_MEMORY, "module allocation failed");
    module->fingerprint = fingerprint64(bytes.data, bytes.size);

    try {
        module->functions.reserve(functions_section->count);
        for (uint32_t index = 0; index < functions_section->count; ++index) {
            const uint8_t* record = bytes.data + functions_section->offset +
                                    static_cast<size_t>(index) * dao::kFunctionRecordSize;
            FunctionRecord function{read_u32(record), read_u32(record + 4), read_u16(record + 8),
                                    read_u16(record + 10)};
            const uint32_t reserved = read_u32(record + 12);
            const uint64_t code_end =
                static_cast<uint64_t>(function.code_offset) + function.code_count;
            if (function.parameter_count > function.register_count ||
                function.register_count > vm->config.max_registers ||
                code_end > code_section->count || reserved != 0) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid function record", index);
            }
            module->functions.push_back(function);
        }

        module->code.reserve(code_section->count);
        for (uint32_t index = 0; index < code_section->count; ++index) {
            const uint8_t* record = bytes.data + code_section->offset +
                                    static_cast<size_t>(index) * dao::kInstructionSize;
            module->code.push_back(dao::Instruction{static_cast<dao::Opcode>(record[0]), record[1],
                                                    read_u16(record + 2), read_u16(record + 4),
                                                    read_u16(record + 6), read_i64(record + 8)});
        }

        module->imports.reserve(imports_section->count);
        std::unordered_map<uint32_t, bool> import_symbols;
        import_symbols.reserve(imports_section->count);
        for (uint32_t index = 0; index < imports_section->count; ++index) {
            const uint8_t* record = bytes.data + imports_section->offset +
                                    static_cast<size_t>(index) * dao::kImportRecordSize;
            const ImportRecord import{read_u32(record), read_u16(record + 4)};
            if (read_u16(record + 6) != 0 || import.parameter_count > vm->config.max_registers ||
                !import_symbols.emplace(import.symbol_id, true).second) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid or duplicate import");
            }
            module->imports.push_back(import);
        }

        for (uint32_t function_index = 0; function_index < module->functions.size();
             ++function_index) {
            const auto& function = module->functions[function_index];
            for (uint32_t pc = 0; pc < function.code_count; ++pc) {
                const dao_status status =
                    verify_instruction(*module, function, module->code[function.code_offset + pc],
                                       function_index, pc, error);
                if (status != DAO_OK) {
                    delete module;
                    return status;
                }
            }
        }

        module->exports.reserve(exports_section->count);
        for (uint32_t index = 0; index < exports_section->count; ++index) {
            const uint8_t* record = bytes.data + exports_section->offset +
                                    static_cast<size_t>(index) * dao::kExportRecordSize;
            const uint32_t symbol = read_u32(record);
            const uint32_t function = read_u32(record + 4);
            if (function >= module->functions.size() ||
                !module->exports.emplace(symbol, function).second) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid or duplicate export");
            }
        }
    } catch (const std::bad_alloc&) {
        delete module;
        return fail(error, DAO_OUT_OF_MEMORY, "module decode allocation failed");
    }

    *out_module = module;
    return DAO_OK;
}

void dao_module_release(dao_module* module) { delete module; }

uint64_t dao_module_fingerprint(const dao_module* module) {
    return module == nullptr ? 0 : module->fingerprint;
}

dao_status dao_module_find_export(const dao_module* module, uint32_t symbol_id,
                                  dao_function* out_function) {
    if (module == nullptr || out_function == nullptr)
        return DAO_INVALID_ARGUMENT;
    const auto found = module->exports.find(symbol_id);
    if (found == module->exports.end())
        return DAO_EXPORT_NOT_FOUND;
    *out_function = found->second;
    return DAO_OK;
}

dao_status dao_vm_call(dao_vm* vm, const dao_module* module, dao_function function,
                       const dao_value* args, size_t arg_count, dao_value* out_value,
                       dao_error* error) {
    clear_error(error);
    if (vm == nullptr || module == nullptr || out_value == nullptr ||
        (arg_count != 0 && args == nullptr)) {
        return fail(error, DAO_INVALID_ARGUMENT, "vm, module, arguments, and output must be valid");
    }
    for (size_t index = 0; index < arg_count; ++index) {
        if (!is_valid_value(args[index])) {
            return fail(error, DAO_TYPE_ERROR, "argument contains an invalid value");
        }
    }
    uint64_t budget = vm->config.max_instructions_per_call;
    return execute_function(vm, module, function, args, arg_count, 0, budget, out_value, error);
}

const char* dao_status_name(dao_status status) {
    switch (status) {
    case DAO_OK:
        return "ok";
    case DAO_INVALID_ARGUMENT:
        return "invalid_argument";
    case DAO_OUT_OF_MEMORY:
        return "out_of_memory";
    case DAO_BAD_MODULE:
        return "bad_module";
    case DAO_VERIFY_ERROR:
        return "verify_error";
    case DAO_EXPORT_NOT_FOUND:
        return "export_not_found";
    case DAO_TYPE_ERROR:
        return "type_error";
    case DAO_DIVIDE_BY_ZERO:
        return "divide_by_zero";
    case DAO_INTEGER_OVERFLOW:
        return "integer_overflow";
    case DAO_CALL_DEPTH_EXCEEDED:
        return "call_depth_exceeded";
    case DAO_INSTRUCTION_LIMIT_EXCEEDED:
        return "instruction_limit_exceeded";
    case DAO_RUNTIME_ERROR:
        return "runtime_error";
    case DAO_IMPORT_NOT_FOUND:
        return "import_not_found";
    }
    return "unknown";
}

} // extern "C"
