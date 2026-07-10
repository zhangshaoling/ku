#ifndef DAO_KERNEL_FORMAT_HPP
#define DAO_KERNEL_FORMAT_HPP

#include <cstdint>
#include <vector>

namespace dao {

inline constexpr uint16_t kFormatVersion = 1;
inline constexpr uint16_t kVmAbiVersion = 3;
inline constexpr uint32_t kHeaderSize = 16;
inline constexpr uint32_t kSectionEntrySize = 16;
inline constexpr uint32_t kFunctionRecordSize = 16;
inline constexpr uint32_t kInstructionSize = 16;
inline constexpr uint32_t kExportRecordSize = 8;
inline constexpr uint32_t kImportRecordSize = 8;

enum class SectionType : uint32_t {
    Functions = 1,
    Code = 2,
    Exports = 3,
    Imports = 4,
};

enum class Opcode : uint8_t {
    Nop = 0,
    LoadI64 = 1,
    Move = 2,
    AddI64 = 3,
    SubI64 = 4,
    MulI64 = 5,
    DivI64 = 6,
    TritNot = 7,
    TritAnd = 8,
    TritOr = 9,
    BranchTritNegative = 10,
    BranchTritZero = 11,
    BranchTritPositive = 12,
    Jump = 13,
    Call = 14,
    Return = 15,
    CallHost = 16,
};

struct Instruction {
    Opcode opcode = Opcode::Nop;
    uint8_t flags = 0;
    uint16_t dst = 0;
    uint16_t a = 0;
    uint16_t b = 0;
    int64_t immediate = 0;
};

struct FunctionSpec {
    uint16_t parameter_count = 0;
    uint16_t register_count = 0;
    std::vector<Instruction> code;
};

class ModuleBuilder {
  public:
    uint32_t add_import(uint32_t symbol_id, uint16_t parameter_count);
    uint32_t add_function(FunctionSpec function);
    void add_export(uint32_t symbol_id, uint32_t function_index);
    std::vector<uint8_t> encode() const;

  private:
    struct ImportSpec {
        uint32_t symbol_id;
        uint16_t parameter_count;
    };

    struct ExportSpec {
        uint32_t symbol_id;
        uint32_t function_index;
    };

    std::vector<ImportSpec> imports_;
    std::vector<FunctionSpec> functions_;
    std::vector<ExportSpec> exports_;
};

} // namespace dao

#endif
