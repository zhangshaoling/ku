#ifndef DAO_KERNEL_MODULE_STORE_HPP
#define DAO_KERNEL_MODULE_STORE_HPP

#include "dao/dao.h"
#include "dao/format.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dao::store {

// Host-owned, filesystem-backed persistence for verified identified modules.
// The store is deliberately policy-light: it preserves module bytes, optional
// source, and a small manifest; linking remains a VM operation.
// Saves use a nonblocking per-directory OS writer lock. Committed bytes and
// provenance are immutable; the manifest publishes last. See MODULE_ABI.md.
class ModuleStore {
  public:
    explicit ModuleStore(std::filesystem::path root);

    dao_status save(const std::vector<uint8_t>& bytes, std::string_view source = {},
                    dao_error* error = nullptr) const;
    dao_status load(std::string_view identity_name, SemanticVersion version,
                    std::vector<uint8_t>* bytes, dao_error* error = nullptr) const;
    dao_status verify(std::string_view identity_name, SemanticVersion version,
                      dao_error* error = nullptr) const;
    dao_status save_source(std::string_view compiler_source, std::string_view source,
                           std::string_view identity_name, SemanticVersion version,
                           dao_error* error = nullptr) const;
    dao_status save_derived_source(std::string_view compiler_source, std::string_view source,
                                   std::string_view identity_name, SemanticVersion version,
                                   std::string_view parent_identity, SemanticVersion parent_version,
                                   dao_error* error = nullptr) const;

    const std::filesystem::path& root() const { return root_; }

  private:
    dao_status save_record(const std::vector<uint8_t>& bytes, std::string_view source,
                           std::string_view parent_identity, SemanticVersion parent_version,
                           dao_error* error) const;
    std::filesystem::path root_;
};

} // namespace dao::store

#endif
