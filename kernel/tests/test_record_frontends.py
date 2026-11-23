"""Compile the same Record sources with both frontends, then run native tests."""
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

COMPILER = str(Path(sys.argv[1]).resolve())


class RecordFrontends(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-record-source-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def run_compiler(self, source, *options, recovery=False):
        path = self.directory / "main.ku"
        path.write_text(source, encoding="utf-8")
        command = [COMPILER] + (["--recovery"] if recovery else [])
        return subprocess.run(command + list(options) + [str(path)], capture_output=True,
                              text=True, encoding="utf-8", timeout=60)

    def check_program(self, source, files=None):
        for name, text in (files or {}).items():
            (self.directory / name).write_text(text, encoding="utf-8")
        path = self.directory / "main.ku"
        path.write_text(source, encoding="utf-8")
        outputs = []
        for recovery in [False, True]:
            with self.subTest(recovery=recovery):
                output = self.directory / f"module-{recovery}.dao"
                command = [COMPILER] + (["--recovery"] if recovery else [])
                result = subprocess.run(command + [str(path), str(output)], capture_output=True,
                                        text=True, encoding="utf-8", timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(output.read_bytes())
                result = self.run_compiler(source, "--test", recovery=recovery)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(outputs), 2)
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[0][4:8], b"\x03\x00\x0b\x00")
        return outputs[0]

    def test_named_fields_and_dynamic_arguments(self):
        self.check_program('''
型 点 { 横: 整数, 纵: 整数 }
思 横坐标(值) { 返 值.横 }
思 测试_记录() {
  值 = 点 { 纵: 2, 横: 40 }
  返 值.横 + 值.纵 == 42 and 横坐标(值) == 40
}
''')

    def test_forward_declaration_empty_and_scalar_fields(self):
        self.check_program('''
思 测试_标量() {
  值 = 数据 { 名称: "中文", 有效: 真, 数值: -42 }
  空记录 = 空型 {}
  返 值.名称 == "中文" and 值.有效 and 值.数值 == -42
}
型 数据 {
  数值: 整数
  有效: 三值
  名称: 文本
}
型 空型 {}
''')

    def test_containers_and_chained_reads(self):
        self.check_program('''
型 点 { 横: 整数 }
思 制作() { 返 点 { 横: 42 } }
思 测试_链式读取() {
  项目 = [点 { 横: 42 }]
  映射 = {"点": 点 { 横: 42 }}
  返 项目[0].横 == 42 and 映射["点"].横 == 42 and 制作().横 == 42
}
''')

    def test_source_imports_rewrite_type_names(self):
        self.check_program('''
引 "dep" 别 d
型 点 { 横: 整数 }
思 测试_导入() {
  值 = d_点 { 横: 40 }
  本地 = 点 { 横: 2 }
  若 d_点 { 横: 42 }.横 != 42 { 返 假 }
  返 d_读取(值) + 本地.横 == 42
}
''', {"dep.ku": "型 点 { 横: 整数 }\n思 读取(值) { 返 值.横 }"})

    def test_initializer_evaluation_order(self):
        self.check_program('''
型 点 { 横: 整数, 纵: 整数 }
思 记录(日志, 值) { push(日志, 值); 返 值 }
思 测试_求值顺序() {
  日志 = []
  值 = 点 { 纵: 记录(日志, 2), 横: 记录(日志, 40) }
  返 日志[0] == 2 and 日志[1] == 40 and 值.横 + 值.纵 == 42
}
''')

    def test_parentheses_and_control_flow(self):
        self.check_program('''
型 点 { 横: i64 }
思 测试_括号() {
  值 = ((点 { 横: (42) }))
  若 (值).横 == 42 { 返 ((点 { 横: -42 })).横 == -42 }
  返 假
}
思 测试_循环() {
  值 = 点 { 横: 42 }
  和 = 0
  遍 项 于 [1] { 和 = 和 + 值.横 }
  当 和 < 42 { 和 = 和 + 1 }
  返 和 == 42
}
''')

    def test_reassignment_and_bindings_keep_dynamic_checks(self):
        self.check_program('''
型 点 { 横: 整数 }
思 测试_重赋值() {
  值 = 点 { 横: 1 }
  值 = {"横": 42}
  值.横 = 40
  返 值.横 + 2 == 42
}
思 测试_循环绑定() {
  值 = 点 { 横: 1 }
  遍 键, 值 于 {"键": {"横": 42}} { 返 值.横 == 42 }
  返 假
}
思 测试_捕获绑定() {
  值 = 点 { 横: 1 }
  试 { 抛 {"横": 42} } 捕 值 { 返 值.横 == 42 }
  返 假
}
思 参数(值) { 值 = 点 { 横: 42 }; 返 值.横 }
思 测试_参数() { 返 参数(空) == 42 }
''')

    def test_imported_type_forward_reference_and_nested_namespace(self):
        self.check_program('''
引 "outer" 别 d
思 测试_导入构造() { 返 d_制作().横 == 42 }
''', {
            "outer.ku": '引 "dep" 别 b\n思 制作() { 返 b_点 { 横: 42 } }',
            "dep.ku": '型 点 { 横: 整数 }',
        })

    def test_invalid_declarations_and_constructors(self):
        invalid = [
            "型 点 { 横: 整数, 横: 整数 }\n思 main() { 返 1 }",
            "型 点 { 横: 整数 }\n型 点 {}\n思 main() { 返 1 }",
            "型 点 { 横: 未知 }\n思 main() { 返 1 }",
            "型 点 { 横: 整数 }\n思 main() { 返 点 {} }",
            "型 点 { 横: 整数 }\n思 main() { 返 点 { 横: 1, 横: 2 } }",
            "型 点 { 横: 整数 }\n思 main() { 返 点 { 横: 1, 纵: 2 } }",
            "型 点 { 横: 整数 }\n思 main() { 返 点 { 横: 真 } }",
            "型 点 { 横: 整数 }\n思 main() { 返 点 { 横: \"1\" } }",
            "型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 1 }; 返 值.纵 }",
            "型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 1 }; 值.横 = 2; 返 1 }",
            "型 点 { 横: 整数 }\n思 main(点) { 返 点 }",
            "型 点 { 横: 整数 }\n思 main() { 点 = 1; 返 点 }",
            "型 点 { 横: 整数 }\n思 点() { 返 1 }",
            '型 点 { 横: 整数 }\n思 main() { 返 点 { 横: (真) } }',
            '型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 1 }; 返 值["横"] }',
            '型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 1 }; 值["横"] = 2; 返 1 }',
            '型 点 { 横: 整数 }\nimport 点(0)\n思 main() { 返 1 }',
            '型 点 {}\n思 main() { 遍 点 于 [] { 返 1 }; 返 0 }',
            '型 点 {}\n思 main() { 遍 键, 点 于 {} { 返 1 }; 返 0 }',
            '型 点 {}\n思 main() { 试 { 抛 1 } 捕 点 { 返 0 }; 返 1 }',
            '型', '型 点', '型 点 {', '型 点 { 横:',
            '型 点 {}\n思 main() { 值 = 点 {}; 返 值.',
            '型 点 {}\n思 main() { 返 点 {',
        ]
        for source in invalid:
            for recovery in [False, True]:
                with self.subTest(source=source, recovery=recovery):
                    result = self.run_compiler(source, "--check", recovery=recovery)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("at offset", result.stderr)
                    self.assertIn("error:", result.stderr)

    def test_imported_diagnostic_points_to_original_source(self):
        (self.directory / "dep.ku").write_text(
            '型 点 { 横: 整数 }\n思 main() { 返 点 { 横: 真 } }', encoding="utf-8")
        for recovery in [False, True]:
            result = self.run_compiler('引 "dep" 别 d\n思 main() { 返 1 }',
                                       "--check", recovery=recovery)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("dep", result.stderr)
            self.assertIn(":2:", result.stderr)
            self.assertIn('2 | 思 main() { 返 点 { 横: 真 } }', result.stderr)

    def test_imported_type_cannot_be_shadowed(self):
        (self.directory / "dep.ku").write_text('型 点 {}', encoding="utf-8")
        for recovery in [False, True]:
            result = self.run_compiler('引 "dep" 别 d\n思 main() { d_点 = 1; 返 1 }',
                                       "--check", recovery=recovery)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("at offset", result.stderr)

    def test_two_aliases_preserve_distinct_same_named_types(self):
        data = self.check_program('''
引 "dep" 别 a
引 "dep" 别 b
思 测试_别名() {
  左 = a_点 { 横: 40 }
  右 = b_点 { 横: 2 }
  返 左.横 + 右.横 == 42
}
''', {"dep.ku": '型 点 { 横: 整数 }'})
        section_count = struct.unpack_from("<I", data, 12)[0]
        sections = [struct.unpack_from("<IIII", data, 16 + index * 16)
                    for index in range(section_count)]
        types = next(section for section in sections if section[0] == 8)
        strings = next(section for section in sections if section[0] == 5)
        self.assertEqual(types[3], 2)
        names = []
        for index in range(2):
            name_index = struct.unpack_from("<I", data, types[1] + index * 16)[0]
            start, length = struct.unpack_from("<II", data, strings[1] + name_index * 8)
            names.append(data[strings[1] + start:strings[1] + start + length].decode("utf-8"))
        self.assertEqual(names, ["a_点", "b_点"])

    def test_import_rewriting_preserves_field_names_matching_declarations(self):
        self.check_program('''
引 "dep" 别 d
思 测试_字段同名() { 返 d_制作().点 + d_制作().读取 == 42 }
''', {"dep.ku": '''
型 点 { 点: 整数, 读取: 整数 }
思 读取() { 返 2 }
思 制作() { 值 = 点 { 点: 40, 读取: 读取() }; 若 值.点 == 40 { 返 值 }; 返 值 }
'''})

    def test_duplicate_field_diagnostic_points_to_repeated_name(self):
        sources = [
            '型 点 {\n  横: 整数,\n  横: 整数\n}\n思 main() { 返 1 }',
            '型 点 { 横: 整数 }\n思 main() { 返 点 { 横: 1,\n  横: 2 } }',
        ]
        for source in sources:
            for recovery in [False, True]:
                result = self.run_compiler(source, "--check", recovery=recovery)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(":3:3: error:", result.stderr)

    def test_sibling_imports_share_dependency_without_false_cycle(self):
        self.check_program('''
引 "left" 别 a
引 "right" 别 b
思 测试_共享依赖() { 返 a_制作().横 + b_制作().横 == 42 }
''', {
            "left.ku": '引 "dep" 别 d\n思 制作() { 返 d_点 { 横: 40 } }',
            "right.ku": '引 "dep" 别 d\n思 制作() { 返 d_点 { 横: 2 } }',
            "dep.ku": '型 点 { 横: 整数 }',
        })

    def test_dynamic_record_operations_fail_and_map_missing_stays_null(self):
        prefix = '型 点 { 横: 整数 }\n思 获取() { 返 点 { 横: 42 } }\n'
        bodies = [
            '值 = 获取(); 返 值.未知',
            '值 = 获取(); 值.横 = 1; 返 真',
            '值 = 获取(); 返 值["横"]',
            '值 = 获取(); 值["横"] = 1; 返 真',
            '值 = 获取(); 返 值 == 值',
            '值 = 获取(); 返 len(值)',
        ]
        for body in bodies:
            for recovery in [False, True]:
                result = self.run_compiler(prefix + '思 测试_动态拒绝() { ' + body + ' }',
                                           "--test", recovery=recovery)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("type_error", result.stdout)
        self.check_program(prefix + '思 测试_Map() { 值 = {}; 返 值.未知 == 空 }')

    def test_formatter_preserves_record_program(self):
        source = '型 点 {横: 整数,纵: 整数}\n思 测试_格式(){值=点 {纵:2,横:40};返 值.横+值.纵==42}'
        result = self.run_compiler(source, "--fmt")
        self.assertEqual(result.returncode, 0, result.stderr)
        formatted = result.stdout
        result = self.run_compiler(formatted, "--fmt")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(formatted, result.stdout)
        self.check_program(formatted)

    def test_wrong_dynamic_field_type_fails_at_runtime(self):
        source = '''
型 点 { 横: 整数 }
思 制作(值) { 返 点 { 横: 值 } }
思 测试_动态错误() { 值 = 制作(真); 返 真 }
'''
        for recovery in [False, True]:
            result = self.run_compiler(source, "--test", recovery=recovery)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("type_error", result.stdout)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
