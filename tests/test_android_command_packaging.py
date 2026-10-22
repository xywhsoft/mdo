"""The APK packager must not reuse a host with an inaccessible worker entry."""
import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/android"))
with patch.dict(os.environ, {"MDO_ANDROID_XSERVER_ROOT": str(ROOT)}):
    spec = importlib.util.spec_from_file_location("mdo_worker_apk", ROOT / "tools/android/build_apk.py")
    packager = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packager)


class AndroidCommandRuntimeTests(unittest.TestCase):
    def test_old_hidden_or_undefined_embedding_entry_is_rejected(self):
        for symbols in ("00001234 T JNI_OnLoad\n", "         U xsHostRun\n", "00001234 t xsHostRun\n"):
            with self.subTest(symbols=symbols), patch.object(packager.subprocess, "check_output", return_value=symbols):
                with self.assertRaisesRegex(ValueError, "rebuild the native host"):
                    packager.verify_command_runtime(Path("libxs.so"), Path("llvm-nm"))

    def test_public_native_entry_is_required_from_the_dynamic_symbol_table(self):
        with patch.object(packager.subprocess, "check_output", return_value="00001234 T xsHostRun\n") as probe:
            packager.verify_command_runtime(Path("libxs.so"), Path("llvm-nm"))
        self.assertEqual(probe.call_args.args[0], ["llvm-nm", "--dynamic", "--defined-only", "--extern-only", "libxs.so"])
