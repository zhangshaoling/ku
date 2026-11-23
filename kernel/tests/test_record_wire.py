"""Run the reviewed R0 byte fixtures against the production C ABI."""
import ctypes as c
import json
import os
from pathlib import Path
import sys
import unittest


library_path = Path(sys.argv[1]).resolve()
DLL_DIRECTORIES = []
if os.name == "nt":
    for directory in [library_path.parent, *(Path(path) for path in
                      os.environ.get("DAO_KERNEL_DLL_DIRS", "").split(os.pathsep) if path)]:
        DLL_DIRECTORIES.append(os.add_dll_directory(str(directory)))
LIBRARY = c.CDLL(str(library_path))
FIXTURES = Path(sys.argv[2])


class Value(c.Structure):
    _fields_ = [("type", c.c_uint32), ("reserved", c.c_uint32), ("payload", c.c_int64)]


class Bytes(c.Structure):
    _fields_ = [("data", c.c_void_p), ("size", c.c_size_t)]


class Error(c.Structure):
    _fields_ = [("code", c.c_int), ("function", c.c_uint32),
                ("instruction", c.c_uint32), ("message", c.c_char * 192)]


LIBRARY.dao_vm_create.argtypes = [c.c_void_p]
LIBRARY.dao_vm_create.restype = c.c_void_p
LIBRARY.dao_vm_destroy.argtypes = [c.c_void_p]
LIBRARY.dao_vm_destroy.restype = None
LIBRARY.dao_module_release.argtypes = [c.c_void_p]
LIBRARY.dao_module_release.restype = None
LIBRARY.dao_vm_load_module.argtypes = [c.c_void_p, Bytes, c.POINTER(c.c_void_p), c.POINTER(Error)]
LIBRARY.dao_vm_call.argtypes = [c.c_void_p, c.c_void_p, c.c_uint32, c.POINTER(Value),
                                c.c_size_t, c.POINTER(Value), c.POINTER(Error)]


class RecordWire(unittest.TestCase):
    def test_disabled_vm_reports_missing_feature(self):
        self.run_case({"features": 0, "patches": [], "load": "DAO_UNSUPPORTED_FEATURE"})

    def test_reviewed_wire_cases(self):
        manifest = json.loads((FIXTURES / "cases.json").read_text(encoding="utf-8"))
        for case in manifest["cases"]:
            with self.subTest(case=case["id"]):
                self.run_case(case)

    def run_case(self, case):
        statuses = {"DAO_OK": 0, "DAO_BAD_MODULE": 3, "DAO_VERIFY_ERROR": 4,
                    "DAO_TYPE_ERROR": 6, "DAO_UNSUPPORTED_FEATURE": 17}
        data = bytearray.fromhex((FIXTURES / "scalar_record_v3.hex").read_text())
        for patch in case["patches"]:
            start, width = patch["offset"], patch["width"]
            data[start:start + width] = patch["value"].to_bytes(width, "little")
        buffer = c.create_string_buffer(bytes(data))
        vm = LIBRARY.dao_vm_create(None)
        self.assertTrue(vm)
        self.addCleanup(LIBRARY.dao_vm_destroy, vm)
        if case["features"]:
            LIBRARY.dao_vm_set_features.argtypes = [c.c_void_p, c.c_uint64]
            self.assertEqual(LIBRARY.dao_vm_set_features(vm, case["features"]), 0)
        module = c.c_void_p()
        error = Error()
        status = LIBRARY.dao_vm_load_module(vm, Bytes(c.addressof(buffer), len(data)),
                                            c.byref(module), c.byref(error))
        if module.value:
            self.addCleanup(LIBRARY.dao_module_release, module)
        self.assertEqual(status, statuses[case["load"]], error.message)
        if status:
            self.assertFalse(module.value)
        if "call" in case:
            result = Value()
            status = LIBRARY.dao_vm_call(vm, module, 0, None, 0, c.byref(result), c.byref(error))
            self.assertEqual(status, statuses[case["call"]], error.message)
            if status == 0:
                self.assertEqual(result.type, 9)
                LIBRARY.dao_value_record_get.argtypes = [c.c_void_p, c.POINTER(Value),
                                                         c.c_uint32, c.POINTER(Value)]
                field = Value()
                self.assertEqual(LIBRARY.dao_value_record_get(vm, c.byref(result), 0,
                                                              c.byref(field)), 0)
                self.assertEqual((field.type, field.payload), (1, case["field_x"]))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
