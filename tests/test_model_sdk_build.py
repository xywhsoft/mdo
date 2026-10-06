"""Compact-host checks must distinguish SDK declarations from runtime calls."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('model_sdk_build', ROOT / 'tools/build_mdo.py')
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class ModelSdkBuildTests(unittest.TestCase):
    def test_public_declarations_do_not_expand_host_but_real_calls_still_require_exports(self):
        lock = json.loads((ROOT / 'deps.lock').read_text(encoding='utf8'))
        with tempfile.TemporaryDirectory(prefix='mdo-model-sdk-') as temporary:
            base = Path(temporary)
            app = base / 'app'; app.mkdir()
            host = base / 'xs.exe'; host.write_bytes(b'fixture-host')
            receipt = {'schema_version': 1, 'profile_sha256': BUILD.host_profile_digest(False),
                'revision': lock['xserver']['commit'], 'extensions': lock['xserver']['required_extensions'],
                'binary_sha256': hashlib.sha256(host.read_bytes()).hexdigest(), 'xrt_symbols': ['xrtFree']}
            host.with_name(host.name + '.build.json').write_text(json.dumps(receipt), encoding='utf8')
            (app / 'sdk.h').write_text('XRT_API void xrtUnusedApi(void);\n', encoding='utf8')
            source = app / 'client.c'
            source.write_text('/* xrtCommentOnly(); */\nconst char* text="xrtTextOnly()";\nvoid f(void){xrtFree(0);}\n', encoding='utf8')
            with patch.object(BUILD, 'APP', app):
                BUILD.verify_host_receipt(host, lock, False)
                source.write_text('void f(void){xrtUnusedApi();}\n', encoding='utf8')
                with self.assertRaisesRegex(BUILD.BuildError, 'xrtUnusedApi'):
                    BUILD.verify_host_receipt(host, lock, False)


if __name__ == '__main__': unittest.main(verbosity=2)
