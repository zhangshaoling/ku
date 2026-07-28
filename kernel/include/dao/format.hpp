#ifndef DAO_KERNEL_FORMAT_HPP
#define DAO_KERNEL_FORMAT_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dao {

inline constexpr uint16_t kLegacyFormatVersion = 1;
inline constexpr uint16_t kLegacyVmAbiVersion = 9;
inline constexpr uint16_t kFormatVersion = 2;
inline constexpr uint16_t kVmAbiVersion = 10;
inline constexpr uint32_t kHeaderSize = 16;
inline constexpr uint32_t kSectionEntrySize = 16;
inline constexpr uint32_t kFunctionRecordSize = 16;
inline constexpr uint32_t kInstructionSize = 16;
inline constexpr uint32_t kExportRecordSize = 8;
inline constexpr uint32_t kImportRecordSize = 8;
inline constexpr uint32_t kDataRecordSize = 8;
inline constexpr uint32_t kModuleMetadataRecordSize = 24;
inline constexpr uint32_t kModuleImportRecordSize = 24;

enum class SectionType : uint32_t {
    Functions = 1,
    Code = 2,
    Exports = 3,
    Imports = 4,
    Data = 5,
    Metadata = 6,
    ModuleImports = 7,
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
    LoadTrit = 17,
    RemI64 = 18,
    CompareEqI64 = 19,
    CompareNeI64 = 20,
    CompareLtI64 = 21,
    CompareLeI64 = 22,
    CompareGtI64 = 23,
    CompareGeI64 = 24,
    LoadString = 25,
    MakeList = 26,
    ListLength = 27,
    ListGet = 28,
    MakeMap = 29,
    IndexGet = 30,
    TryBegin = 31,
    TryEnd = 32,
    Throw = 33,
    Catch = 34,
    LoadNull = 35,
    IndexSet = 36,
    ListAppend = 37,
    LoadFunction = 38,
    CallValue = 39,
    MakeClosure = 40,
    CallModule = 41,
};

struct SemanticVersion {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
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
    uint32_t add_string(std::string_view value);
    uint32_t add_import(uint32_t symbol_id, uint16_t parameter_count);
    void set_identity(std::string_view name, SemanticVersion version);
    uint32_t add_module_import(std::string_view module_name, SemanticVersion version,
                               uint32_t symbol_id, uint16_t parameter_count);
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

    struct ModuleImportSpec {
        std::string module_name;
        SemanticVersion version;
        uint32_t symbol_id;
        uint16_t parameter_count;
    };

    std::vector<ImportSpec> imports_;
    std::vector<FunctionSpec> functions_;
    std::vector<ExportSpec> exports_;
    std::vector<std::string> strings_;
    bool has_identity_ = false;
    std::string identity_name_;
    SemanticVersion identity_version_{};
    std::vector<ModuleImportSpec> module_imports_;
};

} // namespace dao

#endif
