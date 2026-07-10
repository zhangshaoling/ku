#ifndef DAO_KERNEL_KU_MIGRATION_HPP
#define DAO_KERNEL_KU_MIGRATION_HPP

#include "dao/assemble.hpp"
#include "dao/dao.h"
#include "dao/format.hpp"

#include <string>
#include <vector>

namespace dao {
namespace km {

struct Options {
    bool trit_branches = true;
    bool verify = true;
    bool keep_text = false;
};

dao_status compile(const std::string& source,
                   ModuleBuilder* builder,
                   dao_error* error,
                   const Options* options = nullptr);

} // namespace km
} // namespace dao

#endif