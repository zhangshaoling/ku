"""Exercise persistence guarantees through the production native CLIs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPILER, MODULE_CLI = sys.argv[1:3]


class ModuleStoreContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-store-contract-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = self.root / "store"
        self.source = "思 main() { 返 42 }\n"

    def save(self, identity, version="1.0.0", source=None, parent=None):
        source_file = self.root / "input.ku"
        source_file.write_text(self.source if source is None else source, encoding="utf-8")
        command = [COMPILER, "--store", str(self.store), "--identity", identity, version]
        if parent is None:
            command.append("--recovery")
        else:
            command += ["--parent", parent[0], parent[1]]
        return subprocess.run(command + [str(source_file)], capture_output=True, text=True)

    def verify(self, identity, version="1.0.0"):
        return subprocess.run(
            [MODULE_CLI, "verify", str(self.store), identity, version],
            capture_output=True, text=True,
        )

    def record(self, identity, version="1.0.0"):
        for path in self.store.glob(f"*/v{version}.json"):
            if json.loads(path.read_text(encoding="utf-8"))["identity"] == identity:
                return path
        self.fail(f"record not found: {identity}@{version}")

    def assert_ok(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_derived_retry_and_parent_are_immutable(self):
        self.assert_ok(self.save("ku:agent/parent"))
        self.assert_ok(self.save("ku:agent/other-parent"))
        arguments = ("ku:agent/child", "2.0.0")
        parent = ("ku:agent/parent", "1.0.0")
        self.assert_ok(self.save(*arguments, parent=parent))
        path = self.record(*arguments)
        original = path.read_bytes()
        self.assert_ok(self.save(*arguments, parent=parent))
        self.assertEqual(path.read_bytes(), original)
        result = self.save(*arguments, parent=("ku:agent/other-parent", "1.0.0"))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), original)
        self.assert_ok(self.verify(*arguments))

    def test_parent_must_exist_and_cannot_be_self(self):
        result = self.save("ku:agent/child", parent=("ku:agent/missing", "1.0.0"))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(list(self.store.glob("*/v1.0.0.json")))
        result = self.save("ku:agent/self", parent=("ku:agent/self", "1.0.0"))
        self.assertNotEqual(result.returncode, 0)

    def test_source_provenance_is_immutable(self):
        identity = "ku:agent/source"
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        original = path.read_bytes()
        result = self.save(identity, source="// different provenance\n" + self.source)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(path.with_suffix(".ku").read_text(encoding="utf-8"), self.source)

    def test_manifest_is_parsed_as_a_complete_object(self):
        identity = "ku:agent/manifest"
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        text = path.read_text(encoding="utf-8")
        invalid = [
            text + "garbage",
            '{"nested":' + text + "}",
            text[:-1] + ',"identity":"ku:agent/wrong"}',
            text.replace('"schema_version":1', '"schema_version":10'),
            text[:-1] + ',"parent_identity":"ku:agent/missing"}',
        ]
        for value in invalid:
            with self.subTest(manifest=value):
                path.write_text(value, encoding="utf-8")
                self.assertNotEqual(self.verify(identity).returncode, 0)
                self.assertNotEqual(self.save(identity).returncode, 0)
        path.write_text(json.dumps(json.loads(text), sort_keys=True, indent=2), encoding="utf-8")
        self.assert_ok(self.verify(identity))

    def test_json_identity_escaping(self):
        identity = 'ku:agent/escaped"\\'
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        manifest = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["identity"], identity)
        path.write_text(json.dumps(manifest, ensure_ascii=True), encoding="utf-8")
        self.assert_ok(self.verify(identity))

    def test_source_write_failure_keeps_record_uncommitted(self):
        identity = "ku:agent/retry"
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        path.unlink()
        source = path.with_suffix(".ku")
        source.unlink()
        source.mkdir()
        try:
            self.assertNotEqual(self.save(identity).returncode, 0)
            self.assertFalse(path.exists())
        finally:
            source.rmdir()
        self.assert_ok(self.save(identity))
        self.assert_ok(self.verify(identity))

    def test_missing_source_is_detected_and_recovered_on_retry(self):
        identity = "ku:agent/source-recovery"
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        original = path.read_bytes()
        path.with_suffix(".ku").unlink()
        self.assertNotEqual(self.verify(identity).returncode, 0)
        self.assert_ok(self.save(identity))
        self.assertEqual(path.read_bytes(), original)
        self.assert_ok(self.verify(identity))

    def test_legacy_manifest_without_source_fingerprint(self):
        identity = "ku:agent/legacy"
        self.assert_ok(self.save(identity))
        path = self.record(identity)
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest.pop("source_fingerprint", None)
        path.write_text(json.dumps(manifest), encoding="utf-8")
        self.assert_ok(self.verify(identity))
        self.assert_ok(self.save(identity))

    def test_legacy_identical_parent_fields_remain_readable(self):
        self.assert_ok(self.save("ku:agent/parent"))
        parent = ("ku:agent/parent", "1.0.0")
        self.assert_ok(self.save("ku:agent/legacy-child", parent=parent))
        path = self.record("ku:agent/legacy-child")
        text = path.read_text(encoding="utf-8")
        path.write_text(text[:-1] + ',"parent_identity":"ku:agent/parent",'
                        '"parent_version":"1.0.0"}', encoding="utf-8")
        self.assert_ok(self.verify("ku:agent/legacy-child"))
        self.assert_ok(self.save("ku:agent/legacy-child", parent=parent))
        path.write_text(text[:-1] + ',"parent_identity":"ku:agent/wrong"}', encoding="utf-8")
        self.assertNotEqual(self.verify("ku:agent/legacy-child").returncode, 0)

    def test_manifest_publish_failure_can_retry_derived_save(self):
        self.assert_ok(self.save("ku:agent/parent"))
        parent = ("ku:agent/parent", "1.0.0")
        identity = "ku:agent/derived-recovery"
        self.assert_ok(self.save(identity, parent=parent))
        path = self.record(identity)
        path.unlink()
        path.with_suffix(".ku").unlink()
        path.with_suffix(".dao").unlink()
        temporary = path.with_suffix(".json.tmp")
        temporary.mkdir()
        try:
            self.assertNotEqual(self.save(identity, parent=parent).returncode, 0)
            self.assertFalse(path.exists())
            self.assertNotEqual(self.verify(identity).returncode, 0)
        finally:
            temporary.rmdir()
        self.assert_ok(self.save(identity, parent=parent))
        self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["parent_identity"], parent[0])
        self.assert_ok(self.verify(identity))

    def test_writer_lock_and_process_exit(self):
        identity = "ku:agent/lock-parent"
        self.assert_ok(self.save(identity))
        code = """
import os, sys
lock = open(sys.argv[1], 'a+b')
if os.name == 'nt':
    import msvcrt
    lock.seek(0)
    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
else:
    import fcntl
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
print('locked', flush=True)
sys.stdin.read()
"""
        process = subprocess.Popen(
            [sys.executable, "-c", code, str(self.store / ".write.lock")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            self.assertEqual(process.stdout.readline().strip(), "locked")
            self.assert_ok(self.verify(identity))
            result = self.save("ku:agent/locked-child")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("busy", result.stderr)
        finally:
            process.kill()
            process.communicate(timeout=10)
        self.assert_ok(self.save("ku:agent/locked-child"))
        self.assert_ok(self.verify("ku:agent/locked-child"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
