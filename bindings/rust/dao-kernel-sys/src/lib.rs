#![allow(non_camel_case_types)]
use core::ffi::{c_char, c_void};

pub enum dao_vm {}
pub enum dao_module {}
pub type dao_function = u32;

#[repr(C)] pub struct dao_bytes { pub data: *const u8, pub size: usize }
#[repr(C)] pub struct dao_value { pub type_: u32, pub reserved: u32, pub payload: i64 }
#[repr(C)] pub struct dao_vm_config { pub struct_size: u32, pub max_registers: u32, pub max_call_depth: u32, pub max_cached_modules: u32, pub max_module_bytes: u64, pub max_instructions_per_call: u64 }
#[repr(C)] pub struct dao_error { pub code: i32, pub function_index: u32, pub instruction_index: u32, pub message: [c_char; 192] }

extern "C" {
    pub fn dao_vm_config_default() -> dao_vm_config;
    pub fn dao_vm_create(config: *const dao_vm_config) -> *mut dao_vm;
    pub fn dao_vm_destroy(vm: *mut dao_vm);
    pub fn dao_vm_load_module(vm: *mut dao_vm, bytes: dao_bytes, out: *mut *mut dao_module, error: *mut dao_error) -> i32;
    pub fn dao_module_release(module: *mut dao_module);
    pub fn dao_module_find_export(module: *const dao_module, symbol: u32, out: *mut dao_function) -> i32;
    pub fn dao_vm_call(vm: *mut dao_vm, module: *const dao_module, function: dao_function, args: *const dao_value, count: usize, out: *mut dao_value, error: *mut dao_error) -> i32;
}

const _: Option<*mut c_void> = None;
