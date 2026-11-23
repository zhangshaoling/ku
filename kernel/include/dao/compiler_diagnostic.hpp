#ifndef DAO_COMPILER_DIAGNOSTIC_HPP
#define DAO_COMPILER_DIAGNOSTIC_HPP

#include <charconv>
#include <string>
#include <string_view>

namespace dao {

// Source diagnostics are optional C++ tooling metadata, outside the frozen C ABI.
struct CompilerDiagnostic {
    std::string source_name;
    std::string source;
    size_t offset = std::string::npos;
};

inline size_t diagnostic_offset(std::string_view message) {
    constexpr std::string_view marker = " at offset ";
    const size_t position = message.rfind(marker);
    if (position == std::string_view::npos) return std::string::npos;
    const auto digits = message.substr(position + marker.size());
    size_t offset = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), offset);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size()
               ? offset : std::string::npos;
}

inline void capture_diagnostic(CompilerDiagnostic* diagnostic, std::string_view name,
                               std::string_view source, size_t offset) {
    if (diagnostic == nullptr) return;
    diagnostic->source_name = name;
    diagnostic->source = source;
    diagnostic->offset = offset <= source.size() ? offset : std::string::npos;
}

} // namespace dao

#endif
