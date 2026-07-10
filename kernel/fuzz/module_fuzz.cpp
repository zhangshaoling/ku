#include "dao/dao.h"

#include <cstddef>
#include <cstdint>

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
    return 0;
}
