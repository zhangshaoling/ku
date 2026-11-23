#![allow(non_camel_case_types)]
#![no_std]
use core::ffi::{c_char, c_void};

pub enum dao_vm {}
pub enum dao_module {}
pub type dao_function = u32;

#[repr(C)] pub struct dao_bytes { pub data: *const u8, pub size: usize }
#[repr(C)] pub struct dao_value { pub type_: u32, pub reserved: u32, pub payload: i64 }
#[repr(C)] pub struct dao_vm_config { pub struct_size: u32, pub max_registers: u32, pub max_call_depth: u32, pub reserved: u32, pub max_module_bytes: u64, pub max_instructions_per_call: u64 }
#[repr(C)] pub struct dao_error { pub code: i32, pub function_index: u32, pub instruction_index: u32, pub message: [c_char; 192] }
#[repr(C)] pub struct dao_host_function {
    pub struct_size: u32,
    pub symbol_id: u32,
    pub parameter_count: u32,
    pub reserved: u32,
    pub callback: *const c_void,
    pub user_data: *const c_void,
}

pub const DAO_OK: i32 = 0;
pub const DAO_INVALID_ARGUMENT: i32 = 1;
pub const DAO_RUNTIME_ERROR: i32 = 11;
pub const DAO_UNSUPPORTED_FEATURE: i32 = 17;
pub const DAO_FEATURE_RECORDS: u64 = 1;
pub const DAO_VALUE_NULL: u32 = 0;
pub const DAO_VALUE_I64: u32 = 1;
pub const DAO_VALUE_TRIT: u32 = 2;
pub const DAO_VALUE_BYTES: u32 = 3;
pub const DAO_VALUE_STRING: u32 = 4;
pub const DAO_VALUE_LIST: u32 = 5;
pub const DAO_VALUE_MAP: u32 = 6;
pub const DAO_VALUE_RECORD: u32 = 9;

extern "C" {
    pub fn dao_vm_config_default() -> dao_vm_config;
    pub fn dao_vm_create(config: *const dao_vm_config) -> *mut dao_vm;
    pub fn dao_vm_destroy(vm: *mut dao_vm);
    pub fn dao_vm_set_features(vm: *mut dao_vm, features: u64) -> i32;
    pub fn dao_vm_get_features(vm: *const dao_vm, out: *mut u64) -> i32;
    pub fn dao_module_required_features(module: *const dao_module, out: *mut u64) -> i32;
    pub fn dao_module_record_count(module: *const dao_module, out: *mut u32) -> i32;
    pub fn dao_module_record_name(module: *const dao_module, type_index: u32, out: *mut dao_bytes) -> i32;
    pub fn dao_module_record_field_count(module: *const dao_module, type_index: u32, out: *mut u32) -> i32;
    pub fn dao_module_record_field_info(module: *const dao_module, type_index: u32, slot: u32,
                                      out_name: *mut dao_bytes, out_type: *mut u32) -> i32;
    // The returned owner is retained and must be released with dao_module_release.
    pub fn dao_value_record_type(vm: *const dao_vm, value: *const dao_value,
                                 out_owner: *mut *mut dao_module, out_type: *mut u32) -> i32;
    pub fn dao_value_record_get(vm: *const dao_vm, value: *const dao_value, slot: u32, out: *mut dao_value) -> i32;
    pub fn dao_value_get_view(value: *const dao_value, out: *mut dao_bytes) -> i32;
    pub fn dao_value_make_bytes_view(bytes: dao_bytes, out: *mut dao_value) -> i32;
    pub fn dao_vm_clear_module_cache(vm: *mut dao_vm) -> i32;
    pub fn dao_vm_load_module(vm: *mut dao_vm, bytes: dao_bytes, out: *mut *mut dao_module, error: *mut dao_error) -> i32;
    pub fn dao_module_release(module: *mut dao_module);
    pub fn dao_module_find_export(module: *const dao_module, symbol: u32, out: *mut dao_function) -> i32;
    pub fn dao_vm_call(vm: *mut dao_vm, module: *const dao_module, function: dao_function, args: *const dao_value, count: usize, out: *mut dao_value, error: *mut dao_error) -> i32;
    pub fn dao_value_list_size(vm: *const dao_vm, value: *const dao_value, out: *mut usize) -> i32;
    pub fn dao_value_list_get(vm: *const dao_vm, value: *const dao_value, index: usize, out: *mut dao_value) -> i32;
    pub fn dao_value_map_get(vm: *const dao_vm, value: *const dao_value, key: dao_bytes, out: *mut dao_value) -> i32;
    pub fn dao_vm_make_list(vm: *mut dao_vm, out: *mut dao_value) -> i32;
    pub fn dao_value_list_append(vm: *mut dao_vm, list: *mut dao_value, value: *const dao_value) -> i32;
    pub fn dao_vm_make_map(vm: *mut dao_vm, out: *mut dao_value) -> i32;
    pub fn dao_value_map_set(vm: *mut dao_vm, map: *mut dao_value, key: dao_bytes, value: *const dao_value) -> i32;
    pub fn dao_vm_register_host_function(vm: *mut dao_vm, function: *const dao_host_function) -> i32;
    pub fn dao_vm_unregister_host_function(vm: *mut dao_vm, symbol_id: u32) -> i32;
}

unsafe impl Send for dao_vm {}

const _: Option<*mut c_void> = None;
