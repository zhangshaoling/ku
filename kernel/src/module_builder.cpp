#include "dao/format.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dao {
namespace {

void append_u8(std::vector<uint8_t>& out, uint8_t value) { out.push_back(value); }

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void append_i64(std::vector<uint8_t>& out, int64_t value) {
    const auto bits = static_cast<uint64_t>(value);
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<uint8_t>(bits >> shift));
    }
}

void align_to(std::vector<uint8_t>& out, size_t alignment) {
    while (out.size() % alignment != 0)
        out.push_back(0);
}

struct SectionBytes {
    SectionType type;
    uint32_t count;
    std::vector<uint8_t> bytes;
    uint32_t offset = 0;
};

} // namespace

uint32_t ModuleBuilder::add_import(uint32_t symbol_id, uint16_t parameter_count) {
    const auto duplicate =
        std::find_if(imports_.begin(), imports_.end(),
                     [symbol_id](const ImportSpec& item) { return item.symbol_id == symbol_id; });
    if (duplicate != imports_.end()) {
        throw std::invalid_argument("duplicate import symbol");
    }
    if (imports_.size() >= std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("too many imports");
    }
    imports_.push_back({symbol_id, parameter_count});
    return static_cast<uint32_t>(imports_.size() - 1);
}

uint32_t ModuleBuilder::add_function(FunctionSpec function) {
    if (function.parameter_count > function.register_count) {
        throw std::invalid_argument("parameter count exceeds register count");
    }
    if (functions_.size() >= std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("too many functions");
    }
    functions_.push_back(std::move(function));
    return static_cast<uint32_t>(functions_.size() - 1);
}

void ModuleBuilder::add_export(uint32_t symbol_id, uint32_t function_index) {
    if (function_index >= functions_.size()) {
        throw std::out_of_range("export refers to an unknown function");
    }
    const auto duplicate =
        std::find_if(exports_.begin(), exports_.end(),
                     [symbol_id](const ExportSpec& item) { return item.symbol_id == symbol_id; });
    if (duplicate != exports_.end()) {
        throw std::invalid_argument("duplicate export symbol");
    }
    exports_.push_back({symbol_id, function_index});
}

std::vector<uint8_t> ModuleBuilder::encode() const {
    SectionBytes functions{SectionType::Functions, static_cast<uint32_t>(functions_.size()), {}, 0};
    SectionBytes code{SectionType::Code, 0, {}, 0};
    SectionBytes exports{SectionType::Exports, static_cast<uint32_t>(exports_.size()), {}, 0};
    SectionBytes imports{SectionType::Imports, static_cast<uint32_t>(imports_.size()), {}, 0};

    for (const auto& item : imports_) {
        append_u32(imports.bytes, item.symbol_id);
        append_u16(imports.bytes, item.parameter_count);
        append_u16(imports.bytes, 0);
    }

    uint32_t code_offset = 0;
    for (const auto& function : functions_) {
        if (function.code.size() > std::numeric_limits<uint32_t>::max() - code_offset) {
            throw std::length_error("too many instructions");
        }
        append_u32(functions.bytes, code_offset);
        append_u32(functions.bytes, static_cast<uint32_t>(function.code.size()));
        append_u16(functions.bytes, function.register_count);
        append_u16(functions.bytes, function.parameter_count);
        append_u32(functions.bytes, 0);

        for (const auto& instruction : function.code) {
            append_u8(code.bytes, static_cast<uint8_t>(instruction.opcode));
            append_u8(code.bytes, instruction.flags);
            append_u16(code.bytes, instruction.dst);
            append_u16(code.bytes, instruction.a);
            append_u16(code.bytes, instruction.b);
            append_i64(code.bytes, instruction.immediate);
            ++code.count;
        }
        code_offset += static_cast<uint32_t>(function.code.size());
    }

    auto sorted_exports = exports_;
    std::sort(sorted_exports.begin(), sorted_exports.end(),
              [](const ExportSpec& left, const ExportSpec& right) {
                  return left.symbol_id < right.symbol_id;
              });
    for (const auto& item : sorted_exports) {
        append_u32(exports.bytes, item.symbol_id);
        append_u32(exports.bytes, item.function_index);
    }

    std::vector<SectionBytes> sections;
    sections.push_back(std::move(functions));
    sections.push_back(std::move(code));
    sections.push_back(std::move(exports));
    sections.push_back(std::move(imports));

    std::vector<uint8_t> out;
    out.insert(out.end(), {'D', 'A', 'O', 0});
    append_u16(out, kFormatVersion);
    append_u16(out, kVmAbiVersion);
    append_u32(out, 0);
    append_u32(out, static_cast<uint32_t>(sections.size()));

    const size_t section_table_start = out.size();
    out.resize(out.size() + sections.size() * kSectionEntrySize, 0);

    for (auto& section : sections) {
        align_to(out, 8);
        if (out.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("module exceeds v1 offset range");
        }
        section.offset = static_cast<uint32_t>(out.size());
        out.insert(out.end(), section.bytes.begin(), section.bytes.end());
    }

    size_t cursor = section_table_start;
    for (const auto& section : sections) {
        auto write_u32 = [&out, &cursor](uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8) {
                out[cursor++] = static_cast<uint8_t>(value >> shift);
            }
        };
        write_u32(static_cast<uint32_t>(section.type));
        write_u32(section.offset);
        write_u32(static_cast<uint32_t>(section.bytes.size()));
        write_u32(section.count);
    }

    return out;
}

} // namespace dao
