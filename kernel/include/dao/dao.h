#ifndef DAO_KERNEL_DAO_H
#define DAO_KERNEL_DAO_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(DAO_KERNEL_SHARED)
    #if defined(DAO_KERNEL_BUILD)
        #define DAO_API __declspec(dllexport)
    #else
        #define DAO_API __declspec(dllimport)
    #endif
#else
    #define DAO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dao_vm dao_vm;
typedef struct dao_module dao_module;
typedef uint32_t dao_function;

typedef enum dao_status {
    DAO_OK = 0,
    DAO_INVALID_ARGUMENT = 1,
    DAO_OUT_OF_MEMORY = 2,
    DAO_BAD_MODULE = 3,
    DAO_VERIFY_ERROR = 4,
    DAO_EXPORT_NOT_FOUND = 5,
    DAO_TYPE_ERROR = 6,
    DAO_DIVIDE_BY_ZERO = 7,
    DAO_INTEGER_OVERFLOW = 8,
    DAO_CALL_DEPTH_EXCEEDED = 9,
    DAO_INSTRUCTION_LIMIT_EXCEEDED = 10,
    DAO_RUNTIME_ERROR = 11,
    DAO_IMPORT_NOT_FOUND = 12
} dao_status;

typedef enum dao_value_type {
    DAO_VALUE_NULL = 0,
    DAO_VALUE_I64 = 1,
    DAO_VALUE_TRIT = 2,
    DAO_VALUE_BYTES = 3,
    DAO_VALUE_STRING = 4
} dao_value_type;

typedef struct dao_value {
    uint32_t type;
    /* Zero for scalars; byte length for borrowed bytes/string views. */
    uint32_t reserved;
    /* Scalar bits or a borrowed pointer encoded through intptr_t. */
    int64_t payload;
} dao_value;

typedef struct dao_bytes {
    const uint8_t* data;
    size_t size;
} dao_bytes;

typedef struct dao_vm_config {
    uint32_t struct_size;
    uint32_t max_registers;
    uint32_t max_call_depth;
    uint32_t reserved;
    uint64_t max_module_bytes;
    uint64_t max_instructions_per_call;
} dao_vm_config;

typedef struct dao_error {
    dao_status code;
    uint32_t function_index;
    uint32_t instruction_index;
    char message[192];
} dao_error;

typedef dao_status (*dao_host_callback)(void* user_data, const dao_value* args, size_t arg_count,
                                        dao_value* out_value);

typedef struct dao_host_function {
    uint32_t struct_size;
    uint32_t symbol_id;
    uint32_t parameter_count;
    uint32_t reserved;
    dao_host_callback callback;
    void* user_data;
} dao_host_function;

DAO_API dao_status dao_value_make_bytes_view(dao_bytes bytes, dao_value* out_value);
DAO_API dao_status dao_value_make_string_view(dao_bytes utf8, dao_value* out_value);
DAO_API dao_status dao_value_get_view(const dao_value* value, dao_bytes* out_bytes);

DAO_API dao_vm_config dao_vm_config_default(void);
DAO_API dao_vm* dao_vm_create(const dao_vm_config* config);
DAO_API void dao_vm_destroy(dao_vm* vm);

DAO_API dao_status dao_vm_register_host_function(dao_vm* vm, const dao_host_function* function);
DAO_API dao_status dao_vm_unregister_host_function(dao_vm* vm, uint32_t symbol_id);

DAO_API dao_status dao_vm_load_module(dao_vm* vm, dao_bytes bytes, dao_module** out_module,
                                      dao_error* error);

DAO_API void dao_module_release(dao_module* module);
DAO_API uint64_t dao_module_fingerprint(const dao_module* module);

DAO_API dao_status dao_module_find_export(const dao_module* module, uint32_t symbol_id,
                                          dao_function* out_function);

DAO_API dao_status dao_vm_call(dao_vm* vm, const dao_module* module, dao_function function,
                               const dao_value* args, size_t arg_count, dao_value* out_value,
                               dao_error* error);

DAO_API const char* dao_status_name(dao_status status);

#ifdef __cplusplus
}
#endif

#endif
