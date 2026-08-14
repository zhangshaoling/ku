#include "dao/dao.h"

#include <stdlib.h>
#include <string.h>

static dao_status return_null(void* user_data, const dao_value* args, size_t arg_count,
                              dao_value* out_value) {
    (void)user_data;
    (void)args;
    if (arg_count != 0 || out_value == NULL)
        return DAO_INVALID_ARGUMENT;
    out_value->type = DAO_VALUE_NULL;
    out_value->reserved = 0;
    out_value->payload = 0;
    return DAO_OK;
}

int main(void) {
    if (sizeof(dao_value) != 16)
        return EXIT_FAILURE;

    const uint8_t data[] = {1, 2, 3};
    dao_value view;
    if (dao_value_make_bytes_view((dao_bytes){data, sizeof(data)}, &view) != DAO_OK)
        return EXIT_FAILURE;
    dao_bytes extracted;
    if (dao_value_get_view(&view, &extracted) != DAO_OK || extracted.data != data ||
        extracted.size != sizeof(data)) {
        return EXIT_FAILURE;
    }

    const uint8_t text[] = {'D', 'a', 'o'};
    if (dao_value_make_string_view((dao_bytes){text, sizeof(text)}, &view) != DAO_OK ||
        view.type != DAO_VALUE_STRING) {
        return EXIT_FAILURE;
    }

    dao_vm_config config = dao_vm_config_default();
    if (config.struct_size != sizeof(dao_vm_config))
        return EXIT_FAILURE;
    if (strcmp(dao_status_name(DAO_OK), "ok") != 0)
        return EXIT_FAILURE;

    dao_vm* vm = dao_vm_create(&config);
    if (vm == NULL)
        return EXIT_FAILURE;
    dao_host_function host = {sizeof(dao_host_function), 1, 0, 0, return_null, NULL};
    if (dao_vm_register_host_function(vm, &host) != DAO_OK)
        return EXIT_FAILURE;
    if (dao_vm_unregister_host_function(vm, 1) != DAO_OK)
        return EXIT_FAILURE;
    dao_vm_destroy(vm);
    return EXIT_SUCCESS;
}
