#ifndef DAO_KERNEL_KU_MIGRATION_HPP
#define DAO_KERNEL_KU_MIGRATION_HPP

#include "dao/dao.h"
#include "dao/format.hpp"

#include <string_view>

namespace dao {
namespace km {
struct Options { bool trace = false; };

bool compile(std::string_view source, ModuleBuilder& builder, dao_error* error, Options options = {});

} // namespace km
} // namespace dao

#endif