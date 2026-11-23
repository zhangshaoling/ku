"""Record store opt-in, provenance and numeric CLI execution contracts."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

COMPILER, MODULE = (str(Path(arg).resolve()) for arg in sys.argv[1:3])


class RecordStore(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-record-store-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = self.root / "store"
        self.source = self.root / "main.ku"
        self.source.write_text('型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 42 }; 返 值.横 }',
                               encoding="utf-8")

    def run_tool(self, executable, *arguments):
        return subprocess.run([executable, *map(str, arguments)], capture_output=True,
                              text=True, encoding="utf-8", timeout=30)

    def save(self, enabled=True, recovery=False, version="1.0.0", parent=None):
        options = (["--enable-records"] if enabled else [])
        options += (["--recovery"] if recovery else [])
        options += ["--store", self.store, "--identity", "ku:test/record-store", version]
        if parent:
            options += ["--parent", "ku:test/record-store", parent]
        return self.run_tool(COMPILER, *options, self.source)

    def module(self, command, enabled=True, version="1.0.0"):
        options = ["--enable-records"] if enabled else []
        arguments = [command, self.store, "ku:test/record-store", version]
        if command != "verify":
            arguments += ["main"]
        return self.run_tool(MODULE, *options, *arguments)

    def test_explicit_opt_in_and_default_rejection(self):
        result = self.save(enabled=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.store.exists(), result.stderr)
        result = self.save()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = next(self.store.glob("*/v1.0.0.dao")).read_bytes()
        for command in ["verify", "run", "recall"]:
            result = self.module(command)
            self.assertEqual(result.returncode, 0, result.stderr)
            if command != "verify":
                self.assertEqual(result.stdout.strip(), "42")
            result = self.module(command, enabled=False)
            self.assertNotEqual(result.returncode, 0)
        # Reading with an enabled policy must not authorize a later default read.
        self.assertNotEqual(self.save(enabled=False).returncode, 0)
        self.assertEqual(next(self.store.glob("*/v1.0.0.dao")).read_bytes(), original)

    def test_both_frontends_persist_identical_bytes_and_schema_one(self):
        for recovery in [False, True]:
            result = self.save(recovery=recovery)
            self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(next(self.store.glob("*/v1.0.0.json")).read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema_version"], 1)
        self.assertNotIn("features", manifest)
        self.assertEqual(self.module("run").stdout.strip(), "42")

    def test_derived_parent_uses_same_policy(self):
        self.assertEqual(self.save().returncode, 0)
        # Even a legacy child cannot bypass the policy on a Record parent.
        self.source.write_text('思 main() { 返 43 }', encoding="utf-8")
        result = self.save(enabled=False, version="2.0.0", parent="1.0.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(list(self.store.glob("*/v2.0.0.dao")))
        result = self.save(version="2.0.0", parent="1.0.0")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(next(self.store.glob("*/v2.0.0.json")).read_text(encoding="utf-8"))
        self.assertEqual(manifest["parent_identity"], "ku:test/record-store")
        self.assertEqual(manifest["parent_version"], "1.0.0")
        self.assertEqual(self.module("run", enabled=False, version="2.0.0").stdout.strip(), "43")

    def test_opt_in_does_not_bypass_integrity_or_provenance(self):
        self.assertEqual(self.save().returncode, 0)
        path = next(self.store.glob("*/v1.0.0.ku"))
        path.write_text("tampered source", encoding="utf-8")
        self.assertNotEqual(self.module("verify").returncode, 0)
        self.assertNotEqual(self.module("run").returncode, 0)
        path.write_bytes(self.source.read_bytes())
        self.assertEqual(self.module("verify").returncode, 0)
        binary = next(self.store.glob("*/v1.0.0.dao"))
        changed = bytearray(binary.read_bytes())
        changed[-1] ^= 1
        binary.write_bytes(changed)
        self.assertNotEqual(self.module("verify").returncode, 0)
        self.assertNotEqual(self.module("run").returncode, 0)

    def test_record_result_is_not_printed_as_integer(self):
        for index, expression in enumerate(['点 { 横: 42 }', '[点 { 横: 42 }]', '{"点": 点 { 横: 42 }}']):
            self.source.write_text('型 点 { 横: 整数 }\n思 main() { 返 ' + expression + ' }', encoding="utf-8")
            version = f"1.0.{index}"
            self.assertEqual(self.save(version=version).returncode, 0)
            for command in ["run", "recall"]:
                result = self.module(command, version=version)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIn("Record", result.stderr)

    def test_flag_requires_store_mode_and_does_not_change_legacy(self):
        for options in [["--check"], ["--test"], ["--fmt"], []]:
            result = self.run_tool(COMPILER, "--enable-records", *options, self.source)
            self.assertNotEqual(result.returncode, 0)
        self.source.write_text('思 main() { 返 42 }', encoding="utf-8")
        self.assertEqual(self.save().returncode, 0)
        self.assertEqual(self.module("run", enabled=False).stdout.strip(), "42")

    def test_unused_record_declaration_still_requires_opt_in(self):
        self.source.write_text('型 标记 {}\n思 main() { 返 42 }', encoding="utf-8")
        self.assertNotEqual(self.save(enabled=False).returncode, 0)
        self.assertFalse(self.store.exists())
        self.assertEqual(self.save().returncode, 0)
        self.assertNotEqual(self.module("verify", enabled=False).returncode, 0)
        self.assertEqual(self.module("run").stdout.strip(), "42")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
