#ifndef DAO_KERNEL_KU_MIGRATION_HPP
#define DAO_KERNEL_KU_MIGRATION_HPP

#include "dao/dao.h"
#include "dao/format.hpp"

#include <string>
#include <string_view>

namespace dao {
namespace km {
using ImportResolver = bool (*)(void* user_data, std::string_view module_path,
                                std::string* source, std::string* error);

struct Options {
    bool trace = false;
    ImportResolver import_resolver = nullptr;
    void* import_user_data = nullptr;
};

bool compile(std::string_view source, ModuleBuilder& builder, dao_error* error, Options options = {});

} // namespace km
} // namespace dao

#endif
