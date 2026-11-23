"""Check source locations through both public compiler frontends."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPILER = str(Path(sys.argv[1]).resolve())


class CompilerDiagnostics(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-diagnostics-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def write(self, name, source):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(source.encode("utf-8"))
        return path

    def check_error(self, source, name, line, column, excerpt, *, files=None,
                    mode="--check", caret=None):
        path = self.write("main.ku", source)
        for file_name, contents in (files or {}).items():
            self.write(file_name, contents)
        for recovery in [False, True]:
            with self.subTest(recovery=recovery):
                command = [COMPILER] + ([mode] if mode else [])
                if recovery:
                    command.append("--recovery")
                command.append(str(path))
                if not mode:
                    command.append(str(self.directory / "output.dao"))
                result = subprocess.run(command, capture_output=True,
                                        text=True, encoding="utf-8", timeout=30)
                self.assertEqual(result.returncode, 2 if mode == "--test" else 1)
                self.assertEqual(result.stdout, "")
                location = f"{self.directory / name}:{line}:{column}: error:"
                if sys.platform == "win32":
                    self.assertIn(location.casefold(), result.stderr.casefold())
                else:
                    self.assertIn(location, result.stderr)
                self.assertIn("compile failed", result.stderr)
                self.assertIn(excerpt, result.stderr)
                self.assertIn("^", result.stderr)
                if caret is not None:
                    self.assertEqual(result.stderr.splitlines()[-1], caret)
                self.assertFalse((self.directory / "output.dao").exists())

    def test_chinese_crlf_and_tab(self):
        self.check_error("思 主入口() {\r\n\t返 缺失\r\n}\r\n",
                         "main.ku", 2, 4, "    返 缺失", caret="  | " + " " * 7 + "^")

    def test_syntax_error(self):
        self.check_error("thought main() {\n  return [1 2]\n}\n",
                         "main.ku", 2, 13, "  return [1 2]")

    def test_end_of_file(self):
        self.check_error("thought main() {\n  return 1\n",
                         "main.ku", 3, 1, "3 | ")

    def test_root_location_after_import_expansion(self):
        self.check_error('引 "dep" 别 d\n\n思 main() {\n  返 缺失\n}\n',
                         "main.ku", 4, 5, "  返 缺失",
                         files={"dep.ku": "思 value() { 返 42 }\n"})

    def test_nested_import_location(self):
        self.check_error('引 "wrapper" 别 outer\n思 main() { 返 outer_value() }\n',
                         "sub/leaf.ku", 3, 5, "  返 缺失",
                         files={"wrapper.ku": '引 "sub/leaf" 别 inner\n思 value() { 返 inner_value() }',
                                "sub/leaf.ku": "// 来源\n思 value() {\n  返 缺失\n}\n"})

    def test_nested_import_syntax_error(self):
        self.check_error('引 "wrapper" 别 outer\n思 main() { 返 42 }\n',
                         "leaf.ku", 2, 13, "  return [1 2]",
                         files={"wrapper.ku": '引 "leaf" 别 inner\n思 value() { 返 42 }',
                                "leaf.ku": "thought value() {\n  return [1 2]\n}\n"})

    def test_missing_nested_import_points_to_declaration(self):
        self.check_error('引 "wrapper" 别 outer\n思 main() { 返 42 }\n',
                         "wrapper.ku", 2, 1, '引 "missing" 别 inner',
                         files={"wrapper.ku": '// 来源\n引 "missing" 别 inner\n思 value() { 返 42 }'})

    def test_memory_import_prefix_does_not_shift_location(self):
        self.check_error('思 main() {\n  存 "answer" 42\n  返 缺失\n}\n',
                         "main.ku", 3, 5, "  返 缺失")

    def test_duplicate_parameter(self):
        self.check_error("思 main(参数, 参数) { 返 参数 }\n",
                         "main.ku", 1, 12, "思 main(参数, 参数)")

    def test_duplicate_function(self):
        self.check_error("思 main() { 返 1 }\n思 main() { 返 2 }\n",
                         "main.ku", 2, 3, "思 main() { 返 2 }")

    def test_break_outside_loop(self):
        self.check_error("思 main() {\n  断\n}\n", "main.ku", 2, 3, "  断")

    def test_rewritten_function_name_points_to_original_identifier(self):
        self.check_error('引 "dep" 别 d\n思 main() { 返 42 }\n',
                         "dep.ku", 2, 5, "  返 f()",
                         files={"dep.ku": "思 f(x) {\n  返 f()\n}\n"})

    def test_all_source_commands_share_diagnostics(self):
        for mode in ["", "--fmt", "--test"]:
            with self.subTest(mode=mode):
                self.check_error("thought main() { absent() }", "main.ku", 1, 18,
                                 "thought main() { absent() }", mode=mode)

    def test_invalid_utf8_does_not_invent_a_source_location(self):
        path = self.directory / "invalid.ku"
        path.write_bytes(b"thought main() {\xff}")
        for recovery in [False, True]:
            command = [COMPILER, "--check"] + (["--recovery"] if recovery else [])
            result = subprocess.run(command + [str(path)], capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("not valid UTF-8", result.stderr)
            self.assertNotIn("^", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
