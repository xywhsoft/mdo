"""The canonical native sources must match the dependency actually built."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_mdo


class NativeBrowserSourceTests(unittest.TestCase):
    def test_exact_source_and_stale_patch_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);native=root/'sdk';native.mkdir()
            canonical=root/'browser.hpp';actual=native/'browser.hpp'
            canonical.write_bytes(b'original\n');actual.write_bytes(b'original\r\n')
            lock={'sdk_overlay':{'native_sources':{'browser.hpp':{
                'path':'browser.hpp','sha256':hashlib.sha256(b'original\n').hexdigest()}}}}
            with patch.object(build_mdo,'ROOT',root):
                build_mdo.verify_native_browser_sources(native,lock)
                actual.write_bytes(b'stale\n')
                with self.assertRaisesRegex(build_mdo.BuildError,'mismatch'):
                    build_mdo.verify_native_browser_sources(native,lock)
                actual.write_bytes(b'original\n');canonical.write_bytes(b'changed\n')
                with self.assertRaisesRegex(build_mdo.BuildError,'mismatch'):
                    build_mdo.verify_native_browser_sources(native,lock)

    def test_native_manifest_cannot_escape_the_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);native=root/'sdk';native.mkdir()
            lock={'sdk_overlay':{'native_sources':{'../outside.hpp':{'path':'browser.hpp','sha256':'0'*64}}}}
            with patch.object(build_mdo,'ROOT',root),self.assertRaisesRegex(build_mdo.BuildError,'escaped'):
                build_mdo.verify_native_browser_sources(native,lock)


if __name__=='__main__':unittest.main()
