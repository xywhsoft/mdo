"""Portable pip layout, pinned cache and hostile archive checks (no network)."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime/build"))
from python_runtime import pip_wheel, prepare_python_pip


class PythonPipTests(unittest.TestCase):
    def wheel(self, directory, name="pip/__init__.py", symlink=False):
        path = directory / "fixture.whl"
        with zipfile.ZipFile(path, "w") as archive:
            info = zipfile.ZipInfo(name)
            if symlink:
                info.create_system = 3
                info.external_attr = 0o120777 << 16
            archive.writestr(info, "__version__='26.3'\n")
            archive.writestr("pip-26.3.dist-info/METADATA", "Name: pip\nVersion: 26.3\n")
        return path

    def test_windows_and_android_module_locations(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            wheel = self.wheel(root)
            for windows in (True, False):
                prefix = root / ("windows" if windows else "android")
                library = prefix / ("Lib" if windows else "lib/python3.14")
                library.mkdir(parents=True)
                self.assertEqual(prepare_python_pip(prefix, windows=windows, wheel=wheel), "26.3")
                site = library / "site-packages"
                self.assertTrue((site / "pip/__init__.py").is_file())
                self.assertTrue((site / "_mdo_python_runtime.py").is_file())
                self.assertIn("dont_write_bytecode=True", (site / "mdo-python.pth").read_text())

    def test_unsafe_wheel_cannot_escape_the_runtime(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for name, link in (("../outside.py", False), ("pip/link", True), ("C:/outside.py", False), ("/outside.py", False)):
                wheel = self.wheel(root, name, link)
                with self.assertRaises(ValueError):
                    prepare_python_pip(root / "runtime", windows=True, wheel=wheel)
                self.assertFalse((root / "outside.py").exists())

    def test_corrupted_cached_wheel_is_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            lock = json.loads((ROOT / "tools/runtime/build/pip.lock.json").read_text())
            (root / lock["file"]).write_bytes(b"corrupt cache")
            with self.assertRaisesRegex(ValueError, "cache checksum"):
                pip_wheel(root)


if __name__ == "__main__":
    unittest.main()
