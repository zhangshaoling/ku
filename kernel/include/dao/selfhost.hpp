#ifndef DAO_SELFHOST_HPP
#define DAO_SELFHOST_HPP

#include "dao/dao.h"
#include "dao/format.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dao::selfhost {

using ModuleResolver = bool (*)(void* user_data, std::string_view module_path,
                                std::string* source, std::string* error);

struct Options {
    ModuleResolver module_resolver = nullptr;
    void* module_user_data = nullptr;
    uint64_t instruction_limit = 100ULL * 1000ULL * 1000ULL;
    bool has_identity = false;
    std::string identity_name;
    SemanticVersion identity_version{};
};

bool compile(std::string_view compiler_source, std::string_view source,
             std::vector<uint8_t>* output, dao_error* error, Options options = {});

} // namespace dao::selfhost

#endif
