#include <dao/dao.h>

int main(void) {
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == 0) return 1;
    dao_cache_stats stats = {sizeof(dao_cache_stats), 0, 0, 0};
    const dao_status status = dao_vm_get_cache_stats(vm, &stats);
    dao_vm_destroy(vm);
    return status == DAO_OK ? 0 : 1;
}
