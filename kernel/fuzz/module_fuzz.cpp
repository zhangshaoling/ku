#include "dao/dao.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

struct FuzzVm {
    FuzzVm() {
        dao_vm_config config = dao_vm_config_default();
        config.max_module_bytes = 1024ULL * 1024ULL;
        vm = dao_vm_create(&config);
    }

    ~FuzzVm() { dao_vm_destroy(vm); }

    dao_vm* vm = nullptr;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static FuzzVm state;
    if (state.vm == nullptr)
        return 0;

    dao_module* module = nullptr;
    dao_error error{};
    const dao_status status = dao_vm_load_module(state.vm, dao_bytes{data, size}, &module, &error);
    if (status == DAO_OK) {
        (void)dao_module_fingerprint(module);
        dao_module_release(module);
    }

    dao_value bytes{};
    dao_bytes extracted{};
    if (dao_value_make_bytes_view({data, size}, &bytes) != DAO_OK ||
        dao_value_get_view(&bytes, &extracted) != DAO_OK || extracted.data != data ||
        extracted.size != size) {
        std::abort();
    }

    dao_value string{};
    const dao_status string_status = dao_value_make_string_view({data, size}, &string);
    if (string_status == DAO_OK) {
        if (dao_value_get_view(&string, &extracted) != DAO_OK || extracted.data != data ||
            extracted.size != size) {
            std::abort();
        }
    } else if (string_status != DAO_TYPE_ERROR) {
        std::abort();
    }
    return 0;
}
