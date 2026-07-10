#include "dao/disassemble.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace dao {
namespace {

struct Section {
    SectionType type;
    uint32_t offset;
    uint32_t size;
    uint32_t count;
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

void clear_error(dao_error* error) {
    if (error == nullptr) {
        return;
    }
    std::memset(error, 0, sizeof(*error));
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

const Section* find_section(const std::vector<Section>& sections, SectionType type) {
    for (const auto& section : sections) {
        if (section.type == type) {
            return &section;
        }
    }
    return nullptr;
}

dao_status parse_header(const uint8_t* data, size_t size, uint32_t& section_count,
                        dao_error* error) {
    if (size < 16 || std::memcmp(data, "DAO\0", 4) != 0) {
        return fail(error, DAO_BAD_MODULE, "invalid module magic");
    }
    if (read_u16(data + 4) != kFormatVersion) {
        return fail(error, DAO_BAD_MODULE, "unsupported format version");
    }
    if (read_u16(data + 6) != kVmAbiVersion) {
        return fail(error, DAO_BAD_MODULE, "unsupported VM ABI version");
    }
    if (read_u32(data + 8) != 0) {
        return fail(error, DAO_BAD_MODULE, "unsupported module flags");
    }
    section_count = read_u32(data + 12);
    if (section_count == 0 || section_count > 32) {
        return fail(error, DAO_BAD_MODULE, "invalid section count");
    }
    return DAO_OK;
}

dao_status parse_sections(const uint8_t* data, size_t size, uint32_t section_count,
                          std::vector<Section>& sections, dao_error* error) {
    const uint64_t table_end = static_cast<uint64_t>(kHeaderSize) +
                               static_cast<uint64_t>(section_count) * kSectionEntrySize;
    if (table_end > size) {
        return fail(error, DAO_BAD_MODULE, "truncated section table");
    }
    for (uint32_t index = 0; index < section_count; ++index) {
        const uint8_t* entry = data + kHeaderSize + static_cast<size_t>(index) * kSectionEntrySize;
        Section section{static_cast<SectionType>(read_u32(entry)), read_u32(entry + 4),
                        read_u32(entry + 8), read_u32(entry + 12)};
        if (section.type < SectionType::Functions || section.type > SectionType::Imports) {
            return fail(error, DAO_BAD_MODULE, "unknown section type");
        }
        const uint64_t end = static_cast<uint64_t>(section.offset) + section.size;
        if (section.offset < table_end || end > size) {
            return fail(error, DAO_BAD_MODULE, "section outside module bounds");
        }
        sections.push_back(section);
    }
    return DAO_OK;
}

} // namespace

dao_status disassemble(dao_bytes bytes, DisassembledModule* out_module, dao_error* error) {
    if (out_module == nullptr) {
        return DAO_INVALID_ARGUMENT;
    }
    clear_error(error);

    if (bytes.data == nullptr || bytes.size < kHeaderSize) {
        return fail(error, DAO_BAD_MODULE, "module is too small or data is null");
    }

    uint32_t section_count = 0;
    dao_status status = parse_header(bytes.data, bytes.size, section_count, error);
    if (status != DAO_OK) {
        return status;
    }

    std::vector<Section> sections;
    sections.reserve(section_count);
    status = parse_sections(bytes.data, bytes.size, section_count, sections, error);
    if (status != DAO_OK) {
        return status;
    }

    const Section* functions_section = find_section(sections, SectionType::Functions);
    const Section* code_section = find_section(sections, SectionType::Code);
    const Section* exports_section = find_section(sections, SectionType::Exports);
    const Section* imports_section = find_section(sections, SectionType::Imports);
    if (!functions_section || !code_section || !exports_section || !imports_section) {
        return fail(error, DAO_BAD_MODULE, "required section is missing");
    }

    out_module->imports.reserve(imports_section->count);
    for (uint32_t index = 0; index < imports_section->count; ++index) {
        const uint8_t* record =
            bytes.data + imports_section->offset + static_cast<size_t>(index) * kImportRecordSize;
        out_module->imports.push_back({read_u32(record), read_u16(record + 4)});
    }

    out_module->functions.reserve(functions_section->count);
    for (uint32_t index = 0; index < functions_section->count; ++index) {
        const uint8_t* record = bytes.data + functions_section->offset +
                                static_cast<size_t>(index) * kFunctionRecordSize;
        uint32_t code_offset = read_u32(record);
        uint32_t code_count = read_u32(record + 4);
        uint16_t register_count = read_u16(record + 8);
        uint16_t parameter_count = read_u16(record + 10);

        DisassembledFunction function;
        function.index = index;
        function.register_count = register_count;
        function.parameter_count = parameter_count;
        function.instructions.reserve(code_count);
        for (uint32_t i = 0; i < code_count; ++i) {
            const uint8_t* inst =
                bytes.data + code_section->offset + (code_offset + i) * kInstructionSize;
            DisassembledFunction::Instruction instruction;
            instruction.address = code_offset + i;
            instruction.opcode = static_cast<Opcode>(inst[0]);
            instruction.dst = read_u16(inst + 2);
            instruction.a = read_u16(inst + 4);
            instruction.b = read_u16(inst + 6);
            instruction.immediate = read_i64(inst + 8);
            function.instructions.push_back(instruction);
        }
        out_module->functions.push_back(std::move(function));
    }

    out_module->exports.reserve(exports_section->count);
    for (uint32_t index = 0; index < exports_section->count; ++index) {
        const uint8_t* record =
            bytes.data + exports_section->offset + static_cast<size_t>(index) * kExportRecordSize;
        out_module->exports.push_back({read_u32(record), read_u32(record + 4)});
    }

    return DAO_OK;
}

namespace {

const char* opcode_name(Opcode opcode) {
    switch (opcode) {
    case Opcode::Nop:
        return "NOP";
    case Opcode::LoadI64:
        return "LOAD_I64";
    case Opcode::Move:
        return "MOVE";
    case Opcode::AddI64:
        return "ADD_I64";
    case Opcode::SubI64:
        return "SUB_I64";
    case Opcode::MulI64:
        return "MUL_I64";
    case Opcode::DivI64:
        return "DIV_I64";
    case Opcode::TritNot:
        return "TRIT_NOT";
    case Opcode::TritAnd:
        return "TRIT_AND";
    case Opcode::TritOr:
        return "TRIT_OR";
    case Opcode::BranchTritNegative:
        return "BR_TRIT_NEG";
    case Opcode::BranchTritZero:
        return "BR_TRIT_ZERO";
    case Opcode::BranchTritPositive:
        return "BR_TRIT_POS";
    case Opcode::Jump:
        return "JUMP";
    case Opcode::Call:
        return "CALL";
    case Opcode::Return:
        return "RETURN";
    case Opcode::CallHost:
        return "CALL_HOST";
    }
    return "UNKNOWN";
}

void append_instruction_text(std::ostream& out,
                             const DisassembledFunction::Instruction& instruction) {
    out << "  " << std::setw(5) << std::left << instruction.address << std::setw(0)
        << opcode_name(instruction.opcode);
    switch (instruction.opcode) {
    case Opcode::Nop:
        break;
    case Opcode::LoadI64:
        out << " r" << instruction.dst << ", " << instruction.immediate;
        break;
    case Opcode::Move:
        out << " r" << instruction.dst << ", r" << instruction.a;
        break;
    case Opcode::AddI64:
    case Opcode::SubI64:
    case Opcode::MulI64:
    case Opcode::DivI64:
        out << " r" << instruction.dst << ", r" << instruction.a << ", r" << instruction.b;
        break;
    case Opcode::TritNot:
    case Opcode::TritAnd:
    case Opcode::TritOr:
        out << " r" << instruction.dst << ", r" << instruction.a;
        break;
    case Opcode::BranchTritNegative:
    case Opcode::BranchTritZero:
    case Opcode::BranchTritPositive:
        out << " r" << instruction.a << ", ->" << instruction.immediate;
        break;
    case Opcode::Jump:
        out << " ->" << instruction.immediate;
        break;
    case Opcode::Call:
        out << " fn" << instruction.immediate << ", args r" << instruction.a << ".."
            << (instruction.a + instruction.b - 1) << ", dst r" << instruction.dst;
        break;
    case Opcode::Return:
        out << " r" << instruction.a;
        break;
    case Opcode::CallHost:
        out << " import" << instruction.immediate << ", args r" << instruction.a << ".."
            << (instruction.a + instruction.b - 1) << ", dst r" << instruction.dst;
        break;
    }
}

} // namespace

std::string to_text(const DisassembledModule& module) {
    std::ostringstream out;

    out << "imports: " << module.imports.size() << "\n";
    for (const auto& import : module.imports) {
        out << "  [import " << import.symbol_id << "] params=" << import.parameter_count << "\n";
    }

    out << "exports: " << module.exports.size() << "\n";
    for (const auto& export_ : module.exports) {
        out << "  symbol=" << export_.symbol_id << " -> function " << export_.function_index
            << "\n";
    }

    out << "functions: " << module.functions.size() << "\n";
    for (const auto& function : module.functions) {
        out << "  [function " << function.index << "] registers=" << function.register_count
            << " parameters=" << function.parameter_count
            << " instructions=" << function.instructions.size() << "\n";
        for (const auto& instruction : function.instructions) {
            append_instruction_text(out, instruction);
            out << "\n";
        }
    }

    return out.str();
}

} // namespace dao
