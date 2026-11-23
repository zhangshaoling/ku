"""Keep keys(Map) and similarly named calls identical across both frontends."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

COMPILER = str(Path(sys.argv[1]).resolve())


class KeysFrontends(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-keys-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def compile(self, source, *options, recovery=False):
        path = self.directory / "main.ku"
        path.write_text(source, encoding="utf-8")
        return subprocess.run([COMPILER, *(["--recovery"] if recovery else []),
                               *map(str, options), str(path)], capture_output=True,
                              text=True, encoding="utf-8", timeout=30)

    def check_program(self, source):
        path = self.directory / "main.ku"
        path.write_text(source, encoding="utf-8")
        outputs = []
        for recovery in [False, True]:
            output = self.directory / f"keys-{recovery}.dao"
            result = subprocess.run([COMPILER, *(["--recovery"] if recovery else []),
                                     str(path), str(output)], capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            outputs.append(output.read_bytes())
            result = self.compile(source, "--test", recovery=recovery)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(outputs[0], outputs[1])

    def test_empty_map_sorted_unicode_and_single_evaluation(self):
        self.check_program('''
思 制作(日志) { push(日志, 1); 返 {"中": 3, "z": 2, "a": 1} }
思 测试_keys() {
  日志 = []
  名称 = keys(制作(日志))
  返 len(keys({})) == 0 and len(日志) == 1 and len(名称) == 3 and 名称[0] == "a" and 名称[1] == "z" and 名称[2] == "中"
}
''')

    def test_similarly_named_functions_are_not_builtins(self):
        self.check_program('''
思 keysx(值) { 返 值 + 1 }
思 keys_(值) { 返 值 + 2 }
思 keys中文(值) { 返 值 + 3 }
思 测试_精确名称() { 返 keysx(41) == 42 and keys_(40) == 42 and keys中文(39) == 42 }
''')

    def test_map_containing_record_still_returns_keys(self):
        self.check_program('''
型 标记 {}
思 测试_记录容器() { 名称 = keys({"值": 标记 {}}); 返 len(名称) == 1 and 名称[0] == "值" }
''')

    def test_invalid_arity_and_undefined_prefixed_name(self):
        for expression in ['keys()', 'keys({}, {})', 'keys(', 'keysx({})', 'keys_({})']:
            for recovery in [False, True]:
                result = self.compile('思 main() { 返 ' + expression + ' }', "--check", recovery=recovery)
                self.assertNotEqual(result.returncode, 0, expression)
                self.assertIn("at offset", result.stderr)
                self.assertIn("error:", result.stderr)

    def test_non_map_inputs_remain_type_errors(self):
        for expression in ['42', '空', '"map"', '[]', '标记 {}']:
            source = '型 标记 {}\n思 测试_类型() { 返 keys(' + expression + ') }'
            for recovery in [False, True]:
                result = self.compile(source, "--test", recovery=recovery)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("type_error", result.stdout)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
