"""Inspect this phase's actual candidates; no device, service or network needed."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import build_mdo
from build_android import extract_pack
from test_android_distribution import elf_dependencies, SYSTEM_LIBRARIES


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--native-receipt', type=Path, required=True)
    parser.add_argument('--badging', type=Path, required=True, help='aapt2 dump badging output for this APK')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    lock = build_mdo.load_object(ROOT / 'deps.lock')
    product = build_mdo.load_object(args.exe.with_name('build.json'))
    receipt = build_mdo.load_object(args.native_receipt)
    exe = args.exe.read_bytes()
    assert product['xs_commit'] == receipt['revision'] == lock['xserver']['commit']
    assert not product['mdo_worktree_dirty']
    assert product['sha256'] == digest(exe) and product['bytes'] == len(exe)
    apk_bytes = args.apk.read_bytes()
    badging = args.badging.read_text(encoding='utf-8')
    assert "package: name='org.xleaves.mdo.qa.page_restore' versionCode='30000031'" in badging
    assert 'application-debuggable' in badging
    with tempfile.TemporaryDirectory(prefix='mdo-android-candidate-') as raw:
        directory = Path(raw)
        pack = directory / 'app.xrtpack'
        extract_pack(args.exe, pack)
        with zipfile.ZipFile(args.apk) as apk:
            assert apk.testzip() is None
            names = apk.namelist()
            assert len(names) == len(set(names))
            assert apk.read('assets/app.xrtpack') == pack.read_bytes()
            metadata = json.loads(apk.read('assets/mdo-runtime.json'))
            assert metadata['edition'] == 'lite' and not metadata['files']
            assert int(metadata['build_id']) == 30000031
            libraries = {name for name in names if name.startswith('lib/')}
            assert libraries == {'lib/arm64-v8a/libxs.so'}
            native = apk.read('lib/arm64-v8a/libxs.so')
            assert digest(native) == receipt['binary_sha256']
            assert not elf_dependencies(native, 'libxs.so') - SYSTEM_LIBRARIES
            info = apk.getinfo('lib/arm64-v8a/libxs.so')
            assert info.compress_type == zipfile.ZIP_STORED
            with args.apk.open('rb') as stream:
                stream.seek(info.header_offset)
                header = stream.read(30)
            name_size, extra_size = struct.unpack_from('<HH', header, 26)
            assert (info.header_offset + 30 + name_size + extra_size) % 16384 == 0
            assert any(b'Lorg/xserver/android/PageLocation;' in apk.read(name)
                for name in names if name.endswith('.dex'))
            library = directory / 'libxs.so'
            library.write_bytes(native)
            library.with_name('libxs.so.build.json').write_text(json.dumps(receipt), encoding='utf-8')
            build_mdo.verify_host_receipt(library, lock, False)
        report = {
            'passed': True, 'mdo_source_commit': product['mdo_commit'], 'xs_commit': receipt['revision'],
            'exe': {'bytes': len(exe), 'sha256': digest(exe), 'clean_source': True},
            'apk': {'bytes': len(apk_bytes), 'sha256': digest(apk_bytes),
                'package': 'org.xleaves.mdo.qa.page_restore', 'build_id': 30000031,
                'edition': 'lite', 'debuggable': True, 'route_resolver_in_dex': True,
                'native_zip_and_elf_alignment_16k': True, 'exact_pack_from_exe': True},
            'native': {key: receipt[key] for key in ('revision', 'profile_sha256', 'binary_sha256', 'extensions')},
            'pack_sha256': digest(pack.read_bytes()),
            'device_connected': False, 'native_webview_tested': False, 'daily_app_overwritten': False,
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
