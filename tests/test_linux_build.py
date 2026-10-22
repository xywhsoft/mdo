"""Product validation: target CRT selection and actual ELF linkage checks."""
import importlib.util
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_linux
import build_mdo


class LinuxBuildTests(unittest.TestCase):
    def test_musl_cached_host_requires_the_actual_linked_thread_stack(self):
        with tempfile.TemporaryDirectory() as temporary:
            host = Path(temporary) / 'host'
            header = bytearray(120)
            header[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<Q', header, 32, 64)
            struct.pack_into('<HH', header, 54, 56, 1)
            struct.pack_into('<I', header, 64, 0x6474E551)
            for size in (0, 131072, build_mdo.MUSL_THREAD_STACK_BYTES - 1):
                struct.pack_into('<Q', header, 104, size); host.write_bytes(header)
                with self.assertRaisesRegex(build_mdo.BuildError, '8 MiB'):
                    build_mdo.verify_musl_host_stack(host)
            struct.pack_into('<Q', header, 104, build_mdo.MUSL_THREAD_STACK_BYTES)
            host.write_bytes(header); build_mdo.verify_musl_host_stack(host)
            host.write_bytes(header[:110])
            with self.assertRaisesRegex(build_mdo.BuildError, 'truncated'):
                build_mdo.verify_musl_host_stack(host)

    def test_wsl_log_forwarding_preserves_output_and_failure(self):
        # A real child verifies both streams without depending on WSL being
        # installed. Actual baseline unmounting is checked by the Linux gate.
        command = [sys.executable, '-c',
                   'import sys; print("build-output"); '
                   'print("build-diagnostic", file=sys.stderr); sys.exit(7)']
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                build_linux.run_wsl(command, ROOT, False)
        self.assertEqual(raised.exception.returncode, 7)
        self.assertIn('build-output', output.getvalue())
        self.assertIn('build-diagnostic', output.getvalue())

    def test_wsl_dry_run_does_not_launch_a_child(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            build_linux.run_wsl(['missing-build-executable'], ROOT, True)
        self.assertIn('missing-build-executable', output.getvalue())

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

    @unittest.skipUnless(os.name != "nt" and shutil.which("musl-gcc"), "musl toolchain required")
    def test_musl_default_thread_stack_survives_large_native_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, binary = Path(temporary) / 'stack.c', Path(temporary) / 'stack'
            source.write_text('''#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
static void* probe(void* unused){
 (void)unused;pthread_attr_t attr;size_t size=0;
 if(pthread_getattr_np(pthread_self(),&attr))return (void*)1;
 pthread_attr_getstacksize(&attr,&size);pthread_attr_destroy(&attr);
 return (void*)(uintptr_t)(size<8388608);
}
int main(void){pthread_t thread;void* result;
 return pthread_create(&thread,0,probe,0)||pthread_join(thread,&result)||(uintptr_t)result;
}''')
            subprocess.run(['musl-gcc','-static',str(source),'-pthread',
                            f'-Wl,-z,stack-size={build_mdo.MUSL_THREAD_STACK_BYTES}', '-o',str(binary)], check=True)
            build_mdo.verify_musl_host_stack(binary)
            subprocess.run([str(binary)], check=True)

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
