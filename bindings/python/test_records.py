"""Exercise Python Record bindings against a compiled module and native runtime."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
from dao_kernel import (Runtime, CodeMemory, DaoKernelError, DaoValue, Thought, fnv1a,
                        DAO_FEATURE_RECORDS, DAO_VALUE_RECORD, DAO_VALUE_I64,
                        DAO_VALUE_TRIT, DAO_VALUE_STRING, DAO_VALUE_BYTES)

LIBRARY, COMPILER = map(lambda value: str(Path(value).resolve()), sys.argv[1:3])
SOURCE = '''
import blob(0)
import observe(1)
型 数据 { 数值: 整数, 有效: 三值, 名称: 文本, 内容: 字节 }
型 空型 {}
思 main() { 返 数据 { 名称: "中文", 内容: blob(), 有效: 真, 数值: 42 } }
思 列表() { 返 [main()] }
思 映射() { 返 {"值": main()} }
思 空记录() { 返 空型 {} }
思 数字() { 返 42 }
思 回声(x) { 返 x }
思 边界() { 返 observe([main()]) }
'''


class RecordBinding(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="ku-python-record-")
        cls.addClassCleanup(cls.temporary.cleanup)
        path = Path(cls.temporary.name)
        source, output = path / "main.ku", path / "main.dao"
        source.write_text(SOURCE, encoding="utf-8")
        result = subprocess.run([COMPILER, str(source), str(output)], capture_output=True, timeout=30)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.payload = output.read_bytes()

    def runtime(self, features=DAO_FEATURE_RECORDS):
        runtime = Runtime(LIBRARY, features=features)
        self.addCleanup(runtime.close)
        binary = (c.c_uint8 * 3)(0, 255, 42)
        # The callback owns these borrowed Bytes for the runtime's lifetime.
        runtime.register_host_import("blob", 0, lambda *_: DaoValue(
            DAO_VALUE_BYTES, 3, c.addressof(binary)))
        return runtime

    def test_default_policy_and_locking(self):
        runtime = self.runtime(0)
        self.assertEqual(runtime.features, 0)
        with self.assertRaises(DaoKernelError):
            runtime.load(self.payload)
        runtime.set_features(DAO_FEATURE_RECORDS)
        module = runtime.load(self.payload)
        self.addCleanup(module.close)
        self.assertEqual(module.required_features, DAO_FEATURE_RECORDS)
        runtime.set_features(DAO_FEATURE_RECORDS)
        for mask in [0, 2, -1, 1 << 64]:
            with self.assertRaises((ValueError, DaoKernelError)):
                runtime.set_features(mask)
        with self.assertRaises((ValueError, DaoKernelError)):
            Runtime(LIBRARY, features=2)

    def test_legacy_module_stays_default_and_has_no_records(self):
        path = Path(self.temporary.name)
        source, output = path / "legacy.ku", path / "legacy.dao"
        source.write_text('思 main() { 返 42 }', encoding="utf-8")
        result = subprocess.run([COMPILER, str(source), str(output)], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        with Runtime(LIBRARY) as runtime, runtime.load(output.read_bytes()) as module:
            self.assertEqual(runtime.features, 0)
            self.assertEqual(module.required_features, 0)
            self.assertEqual(module.record_count, 0)
            self.assertEqual(module.call_i64(fnv1a("main")), 42)
            with self.assertRaises(DaoKernelError):
                runtime.set_features(DAO_FEATURE_RECORDS)

    def test_descriptors_scalar_fields_and_nominal_owner(self):
        runtime = self.runtime()
        with runtime.load(self.payload) as module:
            self.assertEqual(module.record_count, 2)
            self.assertEqual(module.record_name(0), "数据")
            self.assertEqual(module.record_field_count(0), 4)
            self.assertEqual([module.record_field_info(0, i) for i in range(4)],
                             [("数值", DAO_VALUE_I64), ("有效", DAO_VALUE_TRIT),
                              ("名称", DAO_VALUE_STRING), ("内容", DAO_VALUE_BYTES)])
            record = module.call(fnv1a("main"))
            self.assertEqual(record.type, DAO_VALUE_RECORD)
            owner, type_index = runtime.record_type(record)
            self.addCleanup(owner.close)
            self.assertEqual(type_index, 0)
            self.assertEqual(owner._handle.value, module._handle.value)
            self.assertEqual(owner.record_name(type_index), "数据")
            self.assertEqual(runtime.record_get(record, 0).payload, 42)
            self.assertEqual(runtime.record_get(record, 1).payload, 1)
            self.assertEqual(runtime.value_text(runtime.record_get(record, 2)), "中文")
            self.assertEqual(runtime.value_bytes(runtime.record_get(record, 3)), b"\x00\xff*")
            for slot in [-1, 4, 1 << 32]:
                with self.assertRaises((ValueError, DaoKernelError)):
                    runtime.record_get(record, slot)
            with self.assertRaises(DaoKernelError):
                runtime.record_get(DaoValue(DAO_VALUE_I64, 0, 42), 0)
            with self.assertRaises(DaoKernelError):
                module.record_field_info(1, 0)
            for index in [-1, 1 << 32]:
                with self.assertRaises(ValueError):
                    module.record_name(index)
                with self.assertRaises(ValueError):
                    module.record_field_info(index, 0)

    def test_owner_retention_stale_foreign_and_closed(self):
        runtime = self.runtime()
        module = runtime.load(self.payload)
        record = module.call(fnv1a("main"))
        owner, index = runtime.record_type(record)
        self.addCleanup(owner.close)
        module.close()
        self.assertEqual(runtime._lib.dao_vm_clear_module_cache(runtime._vm), 0)
        self.assertEqual(owner.record_name(index), "数据")
        self.assertEqual(runtime.record_get(record, 0).payload, 42)
        foreign = self.runtime()
        with self.assertRaises(DaoKernelError):
            foreign.record_get(record, 0)
        disabled = self.runtime(0)
        with self.assertRaises(DaoKernelError):
            disabled.record_type(record)
        self.assertEqual(owner.call_i64(fnv1a("数字")), 42)
        with self.assertRaises(DaoKernelError):
            runtime.record_get(record, 0)
        with self.assertRaises(DaoKernelError):
            runtime.record_type(record)
        runtime.close()
        self.assertEqual(owner.record_name(index), "数据")
        with self.assertRaises(DaoKernelError):
            runtime.record_get(record, 0)
        owner.close()
        with self.assertRaises(DaoKernelError):
            owner.record_name(index)

    def test_containers_empty_and_unsupported_call_paths(self):
        runtime = self.runtime()
        with runtime.load(self.payload) as module:
            record = runtime.list_values(module.call(fnv1a("列表")))[0]
            self.assertEqual(runtime.record_get(record, 0).payload, 42)
            with self.assertRaises(DaoKernelError):
                module.call(fnv1a("回声"), record)
            # Rejected Python input must not advance the generation.
            self.assertEqual(runtime.record_get(record, 0).payload, 42)
            record = runtime.map_value(module.call(fnv1a("映射")), "值")
            self.assertEqual(runtime.record_get(record, 0).payload, 42)
            empty = module.call(fnv1a("空记录"))
            with runtime.record_type(empty)[0] as owner:
                self.assertEqual(owner.record_field_count(1), 0)
            with self.assertRaises(DaoKernelError):
                module.call_i64(fnv1a("main"))
        thought = Thought.from_bytes("record-test", self.payload)
        with self.assertRaises(DaoKernelError):
            thought.call_i64(runtime)

    def test_code_memory_policy(self):
        source = '型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 42 }; 返 值.横 }'
        directory = Path(self.temporary.name) / "memory"
        with patch("dao_kernel.code_memory._find_dao_ku", return_value=Path(COMPILER)):
            disabled = CodeMemory(directory)
            enabled = CodeMemory(directory, features=DAO_FEATURE_RECORDS)
            with self.assertRaises(DaoKernelError):
                disabled.save_source(source, "ku:agent/record")
            for recovery in [False, True]:
                enabled.save_source(source, "ku:agent/record", recovery=recovery)
            self.assertTrue(enabled.verify("ku:agent/record").startswith("verified"))
            self.assertEqual(enabled.run("ku:agent/record"), 42)
            with self.assertRaises(DaoKernelError):
                disabled.verify("ku:agent/record")
            with self.assertRaises(DaoKernelError):
                disabled.run("ku:agent/record")
            with self.assertRaises(ValueError):
                CodeMemory(directory, features=2)
            enabled.save_derived_source(source, "ku:agent/child", (1, 0, 0), "ku:agent/record", (1, 0, 0))
            self.assertEqual(enabled.run("ku:agent/child"), 42)

    def test_nested_record_is_rejected_before_python_host_callback(self):
        runtime = self.runtime()
        calls = []
        runtime.register_host_import("observe", 1, lambda *_: calls.append(1) or DaoValue(DAO_VALUE_I64, 0, 1))
        with runtime.load(self.payload) as module:
            with self.assertRaises(DaoKernelError):
                module.call(fnv1a("边界"))
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
