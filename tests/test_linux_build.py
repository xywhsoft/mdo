"""Product validation: target CRT selection and actual ELF linkage checks."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_linux
import build_mdo


class LinuxBuildTests(unittest.TestCase):
    def test_reused_host_cannot_bypass_the_release_baseline(self):
        receipt = {"linux_abi": {"static_core": True, "glibc_required": None},
                   "gui_linux_abi": {"static_core": False, "glibc_required": "2.34"}}
        with self.assertRaises(build_mdo.BuildError):
            build_mdo.verify_linux_host_abi(receipt, "musl", "native", "2.28")
        build_mdo.verify_linux_host_abi(receipt, "musl", "native", "native")
        build_mdo.verify_linux_host_abi(receipt, "musl", "headless", "2.28")
        with self.assertRaises(build_mdo.BuildError):
            build_mdo.verify_linux_host_abi(receipt, "glibc", "headless", "2.28")
        with self.assertRaises(build_mdo.BuildError):
            build_mdo.verify_linux_host_abi({}, "glibc", "headless", "2.28")

    def test_missing_musl_target_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            with self.assertRaises(build_mdo.BuildError):
                build_linux._copy_musl_sysroot(path / "out", path / "missing", path / "libc.a")

    def test_generated_sysroot_cannot_replace_external_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            marker = path / 'keep'; marker.write_text('keep')
            with self.assertRaises(build_mdo.BuildError):
                build_linux.prepare_musl_sysroot(path)
            self.assertEqual(marker.read_text(), 'keep')

    def test_wsl_path_uses_drive_mount(self):
        if os.name != "nt":
            self.skipTest("Windows path conversion")
        self.assertEqual(build_linux.wsl_path(Path("D:/GIT/mdo")), "/mnt/d/GIT/mdo")

    @unittest.skipUnless(os.name != "nt" and shutil.which("gcc") and shutil.which("readelf"), "ELF toolchain required")
    def test_dynamic_binary_cannot_be_labelled_static(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            source = path / "main.c"; binary = path / "probe"
            source.write_text("int main(void){return 0;}\n")
            subprocess.run(["gcc", str(source), "-o", str(binary)], check=True)
            self.assertIn("libc.so.6", build_linux.inspect_elf(binary, False, None)["elf_needed"])
            with self.assertRaises(build_mdo.BuildError):
                build_linux.inspect_elf(binary, True)

    @unittest.skipUnless(os.name != "nt" and shutil.which("musl-gcc") and shutil.which("readelf"), "musl toolchain required")
    def test_static_musl_has_no_interpreter_or_dynamic_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            source = path / "main.c"; binary = path / "probe"
            source.write_text("int main(void){return 0;}\n")
            subprocess.run(["musl-gcc", "-static", str(source), "-o", str(binary)], check=True)
            value = build_linux.inspect_elf(binary, True)
            self.assertTrue(value["static_core"])
            self.assertEqual(value["elf_needed"], [])
            self.assertIsNone(value["glibc_required"])

    @unittest.skipUnless(os.name != "nt" and shutil.which("gcc") and shutil.which("readelf"), "ELF toolchain required")
    def test_glibc_baseline_is_checked_against_actual_imports(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            source, binary = path / "main.c", path / "probe"
            source.write_text("int main(void){return 0;}\n")
            subprocess.run(["gcc", str(source), "-o", str(binary)], check=True)
            required = build_linux.inspect_elf(binary, False, None)["glibc_required"]
            self.assertIsNotNone(required)
            with self.assertRaises(build_mdo.BuildError):
                build_linux.inspect_elf(binary, False, "2.0")


if __name__ == "__main__":
    unittest.main()
