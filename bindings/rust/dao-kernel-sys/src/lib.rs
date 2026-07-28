#![allow(non_camel_case_types)]
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
pub const DAO_VALUE_I64: u32 = 1;
pub const DAO_VALUE_BYTES: u32 = 3;
pub const DAO_VALUE_STRING: u32 = 4;
pub const DAO_VALUE_LIST: u32 = 5;
pub const DAO_VALUE_MAP: u32 = 6;

extern "C" {
    pub fn dao_vm_config_default() -> dao_vm_config;
    pub fn dao_vm_create(config: *const dao_vm_config) -> *mut dao_vm;
    pub fn dao_vm_destroy(vm: *mut dao_vm);
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