"""Verify the native test runner through its public CLI."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPILER = str(Path(sys.argv[1]).resolve())


class TestCommand(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-test-cli-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def run_source(self, source, *, recovery=False, name="suite.ku"):
        path = self.directory / name
        path.write_text(source, encoding="utf-8")
        command = [COMPILER, "--test"]
        if recovery:
            command.append("--recovery")
        return subprocess.run(command + [str(path)], capture_output=True, text=True,
                              encoding="utf-8", timeout=30)

    def test_passes_in_source_order_with_both_frontends(self):
        source = '''
思 加一(x) { 返 x + 1 }
思 test_second() { 返 加一(41) == 42 }
思 测试_中文() { 返 真 }
思 test_first() { 返 真 }
'''
        for recovery in [False, True]:
            with self.subTest(recovery=recovery):
                result = self.run_source(source, recovery=recovery)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("3 passed, 0 failed, 3 total", result.stdout)
                self.assertLess(result.stdout.index("test_second"), result.stdout.index("测试_中文"))
                self.assertLess(result.stdout.index("测试_中文"), result.stdout.index("test_first"))
        self.assertEqual(sorted(path.name for path in self.directory.iterdir()), ["suite.ku"])

    def test_failures_continue_and_report_reasons(self):
        source = '''
思 test_false() { 返 假 }
思 测试_抛错() { 抛 "expected answer 42" }
思 test_divide() { 返 1 / 0 }
思 test_integer() { 返 1 }
思 test_null() { 返 空 }
思 test_parameter(x) { 返 真 }
思 test_loop() { 当 真 {} }
思 test_after_errors() { 返 真 }
'''
        for recovery in [False, True]:
            with self.subTest(recovery=recovery):
                result = self.run_source(source, recovery=recovery)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("1 passed, 7 failed, 8 total", result.stdout)
                for name in ["test_false", "测试_抛错", "test_divide", "test_integer",
                             "test_null", "test_parameter", "test_loop"]:
                    self.assertIn(f"[FAIL] {name}", result.stdout)
                self.assertIn("expected answer 42", result.stdout)
                self.assertIn("divide_by_zero", result.stdout)
                self.assertIn("instruction_limit_exceeded", result.stdout)
                self.assertIn("[PASS] test_after_errors", result.stdout)

    def test_only_top_level_root_declarations_are_tests(self):
        (self.directory / "dependency.ku").write_text(
            '思 test_imported() { 返 假 }\n思 value() { 返 42 }', encoding="utf-8")
        result = self.run_source('''
引 "dependency" 别 dependency
// 思 test_comment() { 返 假 }
;; 思 测试_注释() { 返 假 }
思 helper() { 返 "思 test_string() { 返 假 }" }
思 测试_入口() { 返 dependency_value() == 42 }
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1 passed, 0 failed, 1 total", result.stdout)
        self.assertNotIn("test_imported", result.stdout)

    def test_no_tests_and_compile_errors_exit_two(self):
        for recovery in [False, True]:
            with self.subTest(recovery=recovery):
                empty = self.run_source("思 main() { 返 真 }", recovery=recovery)
                self.assertEqual(empty.returncode, 2)
                self.assertIn("no tests found", empty.stderr)
                broken = self.run_source("思 test_bad() { 返 missing() }", recovery=recovery)
                self.assertEqual(broken.returncode, 2)
                self.assertIn("compile failed", broken.stderr)
                self.assertNotIn("[PASS]", broken.stdout)

    def test_usage_conflicts_exit_two_without_writes(self):
        path = self.directory / "suite.ku"
        path.write_text("思 test_ok() { 返 真 }", encoding="utf-8")
        cases = [
            ["--test"], ["--test", str(path), str(self.directory / "output.dao")],
            ["--test", "--check", str(path)], ["--check", "--test", str(path)],
            ["--test", "--fmt", str(path)], ["--fmt", "--test", str(path)],
            ["--test", "--test", str(path)],
            ["--test", "--identity", "ku:test", "1.0.0", str(path)],
            ["--test", "--store", str(self.directory / "store"), str(path)],
            ["--test", "--parent", "ku:parent", "1.0.0", str(path)],
            ["--test", str(self.directory / "missing.ku")],
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run([COMPILER, *arguments], capture_output=True, text=True,
                                        encoding="utf-8", timeout=30)
                self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([item.name for item in self.directory.iterdir()], ["suite.ku"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
