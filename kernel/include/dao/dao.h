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
    DAO_IMPORT_NOT_FOUND = 12,
    DAO_MODULE_IDENTITY_MISSING = 13,
    DAO_MODULE_CONFLICT = 14,
    DAO_MODULE_CYCLE = 15,
    DAO_MODULE_VERSION_MISMATCH = 16
} dao_status;

typedef enum dao_value_type {
    DAO_VALUE_NULL = 0,
    DAO_VALUE_I64 = 1,
    DAO_VALUE_TRIT = 2,
    DAO_VALUE_BYTES = 3,
    DAO_VALUE_STRING = 4,
    DAO_VALUE_LIST = 5,
    DAO_VALUE_MAP = 6,
    DAO_VALUE_FUNCTION = 7,
    DAO_VALUE_CLOSURE = 8
} dao_value_type;

typedef struct dao_value {
    uint32_t type;
    /* Zero except for borrowed bytes/string views, where this is the byte length. */
    uint32_t reserved;
    /* Scalar bits, a borrowed pointer, a module-local function index, or an opaque
       VM generation handle. See kernel/OWNERSHIP.md. */
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

typedef struct dao_cache_stats {
    uint32_t struct_size;
    uint32_t module_count;
    uint64_t hits;
    uint64_t misses;
} dao_cache_stats;

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

typedef struct dao_module_identity {
    uint32_t struct_size;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    dao_bytes name;
} dao_module_identity;

DAO_API dao_status dao_value_make_bytes_view(dao_bytes bytes, dao_value* out_value);
DAO_API dao_status dao_value_make_string_view(dao_bytes utf8, dao_value* out_value);
DAO_API dao_status dao_value_get_view(const dao_value* value, dao_bytes* out_bytes);
DAO_API dao_status dao_value_list_size(const dao_vm* vm, const dao_value* value, size_t* out_size);
DAO_API dao_status dao_value_list_get(const dao_vm* vm, const dao_value* value, size_t index,
                                      dao_value* out_value);
DAO_API dao_status dao_value_map_get(const dao_vm* vm, const dao_value* value, dao_bytes utf8_key,
                                     dao_value* out_value);
/* Container construction is valid only from a registered Host callback on vm.
   Created List/Map handles remain valid until the next accepted top-level call on
   that VM. Inserted values may be scalars, borrowed views, or current-generation
   List/Map handles; Function and Closure values are rejected. */
DAO_API dao_status dao_vm_make_list(dao_vm* vm, dao_value* out_value);
DAO_API dao_status dao_value_list_append(dao_vm* vm, dao_value* list, const dao_value* value);
DAO_API dao_status dao_vm_make_map(dao_vm* vm, dao_value* out_value);
DAO_API dao_status dao_value_map_set(dao_vm* vm, dao_value* map, dao_bytes utf8_key,
                                     const dao_value* value);

DAO_API dao_vm_config dao_vm_config_default(void);
DAO_API dao_vm* dao_vm_create(const dao_vm_config* config);
DAO_API void dao_vm_destroy(dao_vm* vm);
DAO_API dao_status dao_vm_get_cache_stats(const dao_vm* vm, dao_cache_stats* out_stats);
DAO_API dao_status dao_vm_clear_module_cache(dao_vm* vm);
DAO_API dao_status dao_vm_set_module_cache_capacity(dao_vm* vm, uint32_t capacity);

DAO_API dao_status dao_vm_register_host_function(dao_vm* vm, const dao_host_function* function);
DAO_API dao_status dao_vm_unregister_host_function(dao_vm* vm, uint32_t symbol_id);

DAO_API dao_status dao_vm_load_module(dao_vm* vm, dao_bytes bytes, dao_module** out_module,
                                      dao_error* error);

DAO_API void dao_module_retain(dao_module* module);
DAO_API void dao_module_release(dao_module* module);
DAO_API uint64_t dao_module_fingerprint(const dao_module* module);
DAO_API dao_status dao_module_get_identity(const dao_module* module,
                                           dao_module_identity* out_identity);
/* Linking retains module until vm is destroyed. Identity/version conflicts and
   resolved import cycles are rejected. Missing dependencies may be linked later. */
DAO_API dao_status dao_vm_link_module(dao_vm* vm, dao_module* module, dao_error* error);
DAO_API dao_status dao_vm_find_module(dao_vm* vm, dao_bytes identity_name,
                                      uint32_t version_major, uint32_t version_minor,
                                      uint32_t version_patch, dao_module** out_module);

DAO_API dao_status dao_module_find_export(const dao_module* module, uint32_t symbol_id,
                                          dao_function* out_function);

/* A VM is externally synchronized. An accepted call invalidates List, Map, and
   Closure handles from the previous top-level call. Top-level arguments may be
   scalars or borrowed Bytes/String views, but not VM-owned or module-local values. */
DAO_API dao_status dao_vm_call(dao_vm* vm, const dao_module* module, dao_function function,
                               const dao_value* args, size_t arg_count, dao_value* out_value,
                               dao_error* error);

DAO_API const char* dao_status_name(dao_status status);

#ifdef __cplusplus
}
#endif

#endif
