#ifndef DAO_SOURCE_HOST_HPP
#define DAO_SOURCE_HOST_HPP

#include "dao/dao.h"
#include <string_view>

namespace dao::source_host {

inline uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) hash = (hash ^ byte) * 16777619u;
    return hash;
}

inline dao_status source_len(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type != DAO_VALUE_STRING) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_I64, 0, args[0].reserved};
    return DAO_OK;
}

inline dao_status source_byte(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 2 || args[0].type != DAO_VALUE_STRING || args[1].type != DAO_VALUE_I64)
        return DAO_TYPE_ERROR;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[0], &bytes) != DAO_OK || args[1].payload < 0 ||
        static_cast<uint64_t>(args[1].payload) >= bytes.size) return DAO_INVALID_ARGUMENT;
    *out = {DAO_VALUE_I64, 0, bytes.data[args[1].payload]};
    return DAO_OK;
}

inline dao_status source_slice(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 3 || args[0].type != DAO_VALUE_STRING || args[1].type != DAO_VALUE_I64 ||
        args[2].type != DAO_VALUE_I64) return DAO_TYPE_ERROR;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[0], &bytes) != DAO_OK || args[1].payload < 0 ||
        args[2].payload < 0 || static_cast<uint64_t>(args[1].payload) > bytes.size ||
        static_cast<uint64_t>(args[2].payload) > bytes.size - args[1].payload)
        return DAO_INVALID_ARGUMENT;
    return dao_value_make_string_view(
        {bytes.data + args[1].payload, static_cast<size_t>(args[2].payload)}, out);
}

// These callbacks borrow input storage and provide no file or network access.
inline dao_status register_functions(dao_vm* vm) {
    const dao_host_function functions[] = {
        {sizeof(dao_host_function), symbol("source_len"), 1, 0, source_len, nullptr},
        {sizeof(dao_host_function), symbol("source_byte"), 2, 0, source_byte, nullptr},
        {sizeof(dao_host_function), symbol("source_slice"), 3, 0, source_slice, nullptr},
    };
    for (const auto& function : functions) {
        const dao_status status = dao_vm_register_host_function(vm, &function);
        if (status != DAO_OK) return status;
    }
    return DAO_OK;
}

} // namespace dao::source_host

#endif
