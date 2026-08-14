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
                        bool& identified_module, dao_error* error) {
    if (size < 16 || std::memcmp(data, "DAO\0", 4) != 0) {
        return fail(error, DAO_BAD_MODULE, "invalid module magic");
    }
    const uint16_t format_version = read_u16(data + 4);
    const uint16_t vm_abi_version = read_u16(data + 6);
    const bool legacy_module = format_version == kLegacyFormatVersion &&
                               vm_abi_version == kLegacyVmAbiVersion;
    identified_module = format_version == kFormatVersion && vm_abi_version == kVmAbiVersion;
    if (!legacy_module && !identified_module)
        return fail(error, DAO_BAD_MODULE, "unsupported format or VM ABI version");
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
                          bool identified_module, std::vector<Section>& sections,
                          dao_error* error) {
    const uint64_t table_end = static_cast<uint64_t>(kHeaderSize) +
                               static_cast<uint64_t>(section_count) * kSectionEntrySize;
    if (table_end > size) {
        return fail(error, DAO_BAD_MODULE, "truncated section table");
    }
    for (uint32_t index = 0; index < section_count; ++index) {
        const uint8_t* entry = data + kHeaderSize + static_cast<size_t>(index) * kSectionEntrySize;
        Section section{static_cast<SectionType>(read_u32(entry)), read_u32(entry + 4),
                        read_u32(entry + 8), read_u32(entry + 12)};
        const SectionType maximum =
            identified_module ? SectionType::ModuleImports : SectionType::Data;
        if (section.type < SectionType::Functions || section.type > maximum) {
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
    bool identified_module = false;
    dao_status status =
        parse_header(bytes.data, bytes.size, section_count, identified_module, error);
    if (status != DAO_OK) {
        return status;
    }

    std::vector<Section> sections;
    sections.reserve(section_count);
    status = parse_sections(bytes.data, bytes.size, section_count, identified_module, sections,
                            error);
    if (status != DAO_OK) {
        return status;
    }

    const Section* functions_section = find_section(sections, SectionType::Functions);
    const Section* code_section = find_section(sections, SectionType::Code);
    const Section* exports_section = find_section(sections, SectionType::Exports);
    const Section* imports_section = find_section(sections, SectionType::Imports);
    const Section* data_section = find_section(sections, SectionType::Data);
    const Section* metadata_section = find_section(sections, SectionType::Metadata);
    const Section* module_imports_section = find_section(sections, SectionType::ModuleImports);
    if (!functions_section || !code_section || !exports_section || !imports_section ||
        !data_section ||
        (identified_module && (!metadata_section || !module_imports_section))) {
        return fail(error, DAO_BAD_MODULE, "required section is missing");
    }

    const uint64_t records_size = static_cast<uint64_t>(data_section->count) * kDataRecordSize;
    if (records_size > data_section->size) return fail(error, DAO_BAD_MODULE, "truncated data records");
    for (uint32_t index = 0; index < data_section->count; ++index) {
        const uint8_t* record = bytes.data + data_section->offset + static_cast<size_t>(index) * kDataRecordSize;
        const uint32_t offset = read_u32(record); const uint32_t length = read_u32(record + 4);
        if (offset < records_size || static_cast<uint64_t>(offset) + length > data_section->size)
            return fail(error, DAO_BAD_MODULE, "invalid data record");
        out_module->strings.emplace_back(reinterpret_cast<const char*>(bytes.data + data_section->offset + offset), length);
    }

    if (identified_module) {
        if (metadata_section->count != 1 ||
            metadata_section->size != kModuleMetadataRecordSize ||
            module_imports_section->size !=
                static_cast<uint64_t>(module_imports_section->count) *
                    kModuleImportRecordSize)
            return fail(error, DAO_BAD_MODULE, "invalid module metadata section");
        const uint8_t* metadata = bytes.data + metadata_section->offset;
        const uint32_t identity_index = read_u32(metadata);
        if (identity_index >= out_module->strings.size())
            return fail(error, DAO_BAD_MODULE, "module identity string is invalid");
        out_module->has_identity = true;
        out_module->identity_name = out_module->strings[identity_index];
        out_module->identity_version =
            {read_u32(metadata + 4), read_u32(metadata + 8), read_u32(metadata + 12)};

        out_module->module_imports.reserve(module_imports_section->count);
        for (uint32_t index = 0; index < module_imports_section->count; ++index) {
            const uint8_t* record = bytes.data + module_imports_section->offset +
                                    static_cast<size_t>(index) * kModuleImportRecordSize;
            const uint32_t name_index = read_u32(record);
            if (name_index >= out_module->strings.size())
                return fail(error, DAO_BAD_MODULE, "module import string is invalid");
            out_module->module_imports.push_back(
                {out_module->strings[name_index],
                 {read_u32(record + 4), read_u32(record + 8), read_u32(record + 12)},
                 read_u32(record + 16), read_u16(record + 20)});
        }
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
    case Opcode::LoadTrit: return "LOAD_TRIT";
    case Opcode::LoadString: return "LOAD_STRING";
    case Opcode::LoadFunction: return "LOAD_FUNCTION";
    case Opcode::LoadNull: return "LOAD_NULL";
    case Opcode::MakeList: return "MAKE_LIST";
    case Opcode::MakeClosure: return "MAKE_CLOSURE";
    case Opcode::ListLength: return "LIST_LEN";
    case Opcode::ListGet: return "LIST_GET";
    case Opcode::ListAppend: return "LIST_APPEND";
    case Opcode::MakeMap: return "MAKE_MAP";
    case Opcode::IndexGet: return "INDEX_GET";
    case Opcode::IndexSet: return "INDEX_SET";
    case Opcode::TryBegin: return "TRY_BEGIN";
    case Opcode::TryEnd: return "TRY_END";
    case Opcode::Throw: return "THROW";
    case Opcode::Catch: return "CATCH";
    case Opcode::RemI64: return "REM_I64";
    case Opcode::CompareEqI64: return "EQ_I64";
    case Opcode::CompareNeI64: return "NE_I64";
    case Opcode::CompareLtI64: return "LT_I64";
    case Opcode::CompareLeI64: return "LE_I64";
    case Opcode::CompareGtI64: return "GT_I64";
    case Opcode::CompareGeI64: return "GE_I64";
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
    case Opcode::CallModule:
        return "CALL_MODULE";
    case Opcode::CallValue:
        return "CALL_VALUE";
    }
    return "UNKNOWN";
}

void append_instruction_text(std::ostream& out,
                             const DisassembledFunction::Instruction& instruction) {
    out << "  " << std::setw(5) << std::left << instruction.address << std::setw(0)
        << opcode_name(instruction.opcode);
    switch (instruction.opcode) {
    case Opcode::Nop:
    case Opcode::TryEnd:
        break;
    case Opcode::LoadI64:
    case Opcode::LoadTrit:
    case Opcode::LoadString:
    case Opcode::LoadFunction:
        out << " r" << instruction.dst << ", " << instruction.immediate;
        break;
    case Opcode::LoadNull:
        out << " r" << instruction.dst;
        break;
    case Opcode::Move:
    case Opcode::ListLength:
    case Opcode::ListAppend:
        out << " r" << instruction.dst << ", r" << instruction.a;
        break;
    case Opcode::AddI64:
    case Opcode::SubI64:
    case Opcode::MulI64:
    case Opcode::DivI64:
    case Opcode::RemI64:
    case Opcode::CompareEqI64:
    case Opcode::CompareNeI64:
    case Opcode::CompareLtI64:
    case Opcode::CompareLeI64:
    case Opcode::CompareGtI64:
    case Opcode::CompareGeI64:
    case Opcode::MakeList:
    case Opcode::MakeMap:
    case Opcode::ListGet:
    case Opcode::IndexGet:
    case Opcode::IndexSet:
        out << " r" << instruction.dst << ", r" << instruction.a << ", r" << instruction.b;
        break;
    case Opcode::MakeClosure:
        out << " r" << instruction.dst << ", r" << instruction.a << ", "
            << instruction.b << ", " << instruction.immediate;
        break;
    case Opcode::TritNot:
        out << " r" << instruction.dst << ", r" << instruction.a;
        break;
    case Opcode::TritAnd:
    case Opcode::TritOr:
        out << " r" << instruction.dst << ", r" << instruction.a << ", r" << instruction.b;
        break;
    case Opcode::BranchTritNegative:
    case Opcode::BranchTritZero:
    case Opcode::BranchTritPositive:
        out << " r" << instruction.a << ", ->" << instruction.immediate;
        break;
    case Opcode::Jump:
        out << " ->" << instruction.immediate;
        break;
    case Opcode::TryBegin:
        out << " ->" << instruction.immediate;
        break;
    case Opcode::Throw:
        out << " r" << instruction.a;
        break;
    case Opcode::Catch:
        out << " r" << instruction.dst;
        break;
    case Opcode::Call:
        out << " fn" << instruction.immediate << ", args ";
        if (instruction.b == 0) out << "none"; else out << "r" << instruction.a << ".." << (instruction.a + instruction.b - 1);
        out << ", dst r" << instruction.dst;
        break;
    case Opcode::Return:
        out << " r" << instruction.a;
        break;
    case Opcode::CallHost:
        out << " import" << instruction.immediate << ", args ";
        if (instruction.b == 0) out << "none"; else out << "r" << instruction.a << ".." << (instruction.a + instruction.b - 1);
        out << ", dst r" << instruction.dst;
        break;
    case Opcode::CallModule:
        out << " module_import" << instruction.immediate << ", args ";
        if (instruction.b == 0)
            out << "none";
        else
            out << "r" << instruction.a << ".." << (instruction.a + instruction.b - 1);
        out << ", dst r" << instruction.dst;
        break;
    case Opcode::CallValue:
        out << " r" << instruction.dst << ", r" << instruction.a << ", r"
            << instruction.b << ", " << instruction.immediate;
        break;
    }
}

} // namespace

std::string to_text(const DisassembledModule& module) {
    std::ostringstream out;

    if (module.has_identity) {
        out << "module: " << module.identity_name << " " << module.identity_version.major << "."
            << module.identity_version.minor << "." << module.identity_version.patch << "\n";
        out << "module_imports: " << module.module_imports.size() << "\n";
        for (const auto& import : module.module_imports) {
            out << "  " << import.module_name << " " << import.version.major << "."
                << import.version.minor << "." << import.version.patch << " symbol="
                << import.symbol_id << " params=" << import.parameter_count << "\n";
        }
    }

    out << "imports: " << module.imports.size() << "\n";
    for (const auto& import : module.imports) {
        out << "  [import " << import.symbol_id << "] params=" << import.parameter_count << "\n";
    }

    out << "exports: " << module.exports.size() << "\n";
    for (const auto& export_ : module.exports) {
        out << "  symbol=" << export_.symbol_id << " -> function " << export_.function_index
            << "\n";
    }

    out << "strings: " << module.strings.size() << "\n";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto& value : module.strings) {
        out << "  hex=";
        for (const unsigned char byte : value) out << hex[byte >> 4] << hex[byte & 15];
        out << "\n";
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
