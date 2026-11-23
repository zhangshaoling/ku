//! Native ABI test probe, compiled by ../test_records.py without a platform CRT.
#![no_std]
use core::ptr::{null, null_mut};
use dao_kernel_sys::*;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! { loop {} }

struct Vm(*mut dao_vm);
impl Drop for Vm { fn drop(&mut self) { unsafe { dao_vm_destroy(self.0) } } }
struct Module(*mut dao_module);
impl Drop for Module { fn drop(&mut self) { unsafe { dao_module_release(self.0) } } }

macro_rules! check {
    ($condition:expr) => { if !$condition { return line!() as i32; } };
}

fn value() -> dao_value { dao_value { type_: DAO_VALUE_NULL, reserved: 0, payload: 0 } }
fn view() -> dao_bytes { dao_bytes { data: null(), size: 0 } }

/// Input is compiled by the native test driver with all four scalar Record fields.
/// Function 0 constructs Point; function 1 returns 42.
#[no_mangle]
pub unsafe extern "C" fn dao_record_binding_probe(data: *const u8, size: usize) -> i32 {
    check!(core::mem::size_of::<dao_value>() == 16);
    let config = dao_vm_config_default();
    let vm = Vm(dao_vm_create(&config));
    let other = Vm(dao_vm_create(&config));
    check!(!vm.0.is_null() && !other.0.is_null());
    let mut mask = 99;
    check!(dao_vm_get_features(vm.0, &mut mask) == DAO_OK && mask == 0);
    let mut raw = null_mut();
    check!(dao_vm_load_module(vm.0, dao_bytes { data, size }, &mut raw, null_mut()) == DAO_UNSUPPORTED_FEATURE);
    check!(raw.is_null());
    check!(dao_vm_set_features(vm.0, 2) == DAO_INVALID_ARGUMENT);
    check!(dao_vm_set_features(vm.0, DAO_FEATURE_RECORDS) == DAO_OK);
    check!(dao_vm_load_module(vm.0, dao_bytes { data, size }, &mut raw, null_mut()) == DAO_OK);
    let module = Module(raw);
    check!(dao_module_required_features(module.0, &mut mask) == DAO_OK && mask == DAO_FEATURE_RECORDS);
    check!(dao_vm_set_features(vm.0, 0) == DAO_INVALID_ARGUMENT);
    check!(dao_vm_set_features(vm.0, DAO_FEATURE_RECORDS) == DAO_OK);
    let mut count = 99;
    check!(dao_module_record_count(module.0, &mut count) == DAO_OK && count == 1);
    check!(dao_module_record_field_count(module.0, 0, &mut count) == DAO_OK && count == 4);
    let mut name = view();
    check!(dao_module_record_name(module.0, 0, &mut name) == DAO_OK);
    check!(core::slice::from_raw_parts(name.data, name.size) == b"Point");
    let mut tag = 99;
    check!(dao_module_record_field_info(module.0, 0, 0, &mut name, &mut tag) == DAO_OK && tag == DAO_VALUE_I64);
    check!(core::slice::from_raw_parts(name.data, name.size) == b"x");
    check!(dao_module_record_field_info(module.0, 0, 99, &mut name, &mut tag) == DAO_INVALID_ARGUMENT);
    check!(name.data.is_null() && name.size == 0 && tag == DAO_VALUE_NULL);
    let mut record = value();
    let blob = [0u8, 255, 42];
    let mut input = value();
    check!(dao_value_make_bytes_view(dao_bytes { data: blob.as_ptr(), size: blob.len() }, &mut input) == DAO_OK);
    check!(dao_vm_call(vm.0, module.0, 0, &input, 1, &mut record, null_mut()) == DAO_OK && record.type_ == DAO_VALUE_RECORD);
    let mut field = value();
    check!(dao_value_record_get(vm.0, &record, 0, &mut field) == DAO_OK && field.type_ == DAO_VALUE_I64 && field.payload == 42);
    check!(dao_value_record_get(vm.0, &record, 1, &mut field) == DAO_OK && field.type_ == DAO_VALUE_TRIT && field.payload == 1);
    check!(dao_value_record_get(vm.0, &record, 2, &mut field) == DAO_OK && field.type_ == DAO_VALUE_STRING);
    check!(dao_value_get_view(&field, &mut name) == DAO_OK);
    check!(core::slice::from_raw_parts(name.data, name.size) == "中文".as_bytes());
    check!(dao_value_record_get(vm.0, &record, 3, &mut field) == DAO_OK && field.type_ == DAO_VALUE_BYTES);
    check!(dao_value_get_view(&field, &mut name) == DAO_OK);
    check!(core::slice::from_raw_parts(name.data, name.size) == blob);
    check!(dao_value_record_get(vm.0, &record, 99, &mut field) == DAO_INVALID_ARGUMENT && field.type_ == DAO_VALUE_NULL);
    let mut owner = null_mut();
    let mut type_index = 99;
    check!(dao_value_record_type(vm.0, &record, &mut owner, &mut type_index) == DAO_OK && owner == module.0 && type_index == 0);
    let retained = Module(owner);
    drop(module);
    check!(dao_vm_clear_module_cache(vm.0) == DAO_OK);
    check!(dao_value_record_get(vm.0, &record, 0, &mut field) == DAO_OK && field.payload == 42);
    check!(dao_value_record_get(other.0, &record, 0, &mut field) == DAO_UNSUPPORTED_FEATURE);
    check!(dao_vm_set_features(other.0, DAO_FEATURE_RECORDS) == DAO_OK);
    check!(dao_value_record_get(other.0, &record, 0, &mut field) == DAO_RUNTIME_ERROR);
    check!(dao_vm_call(vm.0, retained.0, 1, &record, 1, &mut field, null_mut()) == DAO_INVALID_ARGUMENT);
    check!(dao_value_record_get(vm.0, &record, 0, &mut field) == DAO_OK);
    check!(dao_vm_call(vm.0, retained.0, 1, null(), 0, &mut field, null_mut()) == DAO_OK);
    check!(dao_value_record_get(vm.0, &record, 0, &mut field) == DAO_RUNTIME_ERROR && field.type_ == DAO_VALUE_NULL);
    check!(dao_value_record_type(vm.0, &record, &mut raw, &mut type_index) == DAO_RUNTIME_ERROR && raw.is_null() && type_index == 0);
    drop(vm);
    check!(dao_module_record_name(retained.0, 0, &mut name) == DAO_OK);
    0
}
