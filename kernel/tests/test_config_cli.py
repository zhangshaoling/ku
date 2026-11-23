"""Exercise the native host with real configuration files."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PROGRAM = str(Path(sys.argv[1]).resolve())


class ConfigCommand(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ku-config-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def run_config(self, contents):
        path = self.directory / "service.conf"
        path.write_bytes(contents if isinstance(contents, bytes) else contents.encode("utf-8"))
        result = subprocess.run([PROGRAM, str(path)], cwd=self.directory,
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ["service.conf"])
        return result

    def test_valid_utf8_crlf_and_optional_workers(self):
        result = self.run_config("# comment\r\nname = 中文服务\r\nport=65535\r\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith("valid "))
        self.assertEqual(result.stderr, "")

    def test_reports_all_errors_with_lines(self):
        result = self.run_config("name=a\nport=0\nworkers=257\nextra=x\nport=80\nbroken")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(len(result.stderr.splitlines()), 5)
        for line in range(2, 7):
            self.assertIn(f"service.conf:{line}: error:", result.stderr)
        self.assertIn("port must be a decimal integer in 1..65535", result.stderr)
        self.assertIn("duplicate key [port]", result.stderr)

    def test_missing_required_keys_have_no_invented_line(self):
        result = self.run_config("")
        self.assertEqual(result.returncode, 1)
        self.assertIn("service.conf: error: missing required key [name]", result.stderr)
        self.assertIn("service.conf: error: missing required key [port]", result.stderr)
        self.assertNotIn(":0:", result.stderr)

    def test_exact_error_output_contract(self):
        result = self.run_config("name=a\nport=0\nworkers=257\nextra=x\nport=80\nbroken")
        path = str(self.directory / "service.conf")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "".join([
            f"{path}:2: error: port must be a decimal integer in 1..65535 [port]\n",
            f"{path}:3: error: workers must be a decimal integer in 1..256 [workers]\n",
            f"{path}:4: error: unknown key [extra]\n",
            f"{path}:5: error: duplicate key [port]\n",
            f"{path}:6: error: expected key=value\n",
        ]))

    def test_decimal_validation_and_overflow(self):
        for number in ["-1", "+80", "1.5", "80abc", "８０", "9" * 1000]:
            with self.subTest(number=number[:20]):
                result = self.run_config(f"name=a\nport={number}")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("service.conf:2: error:", result.stderr)

    def test_invalid_utf8_is_input_error(self):
        result = self.run_config(b"name=\xff\nport=80")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("configuration must be UTF-8", result.stderr)

    def test_help_usage_and_missing_file(self):
        for arguments, expected in [(["--help"], 0), ([], 2), (["--unknown"], 2),
                                    (["a", "b"], 2), (["missing.conf"], 2)]:
            with self.subTest(arguments=arguments):
                result = subprocess.run([PROGRAM, *arguments], cwd=self.directory,
                                        capture_output=True, text=True, encoding="utf-8", timeout=30)
                self.assertEqual(result.returncode, expected, result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
