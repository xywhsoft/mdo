"""Inspect the current workspace executable and isolated Android QA package."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import sys
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import build_mdo
from test_android_distribution import elf_dependencies, SYSTEM_LIBRARIES


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--apk', type=Path, required=True)
    args = parser.parse_args()
    receipt = json.loads(args.exe.with_name('build.json').read_text(encoding='utf-8'))
    native_receipt = json.loads((HERE / 'native.json').read_text(encoding='utf-8'))
    exe = args.exe.read_bytes()
    assert receipt['mdo_commit'].startswith('b9f42347') and not receipt['mdo_worktree_dirty']
    assert sha(exe) == receipt['sha256'] and len(exe) == receipt['bytes']
    assert (ROOT / 'mdo.exe').read_bytes() == exe
    assert json.loads((ROOT / '.build/product-receipts/mdo.exe.json').read_text(encoding='utf-8')) == receipt
    installed = json.loads((HERE / 'e-install-update.json').read_text(encoding='utf-8'))
    assert Path(installed['path']).read_bytes() == exe
    assert sha(Path(installed['backup']).read_bytes()) == installed['previous_sha256']
    assert installed['sha256'] == sha(exe) and installed['home_inventory_unchanged']
    windows_gate = (HERE / 'windows-native.txt').read_text(encoding='utf-8')
    assert 'PASS latest native portable window: four launches, Home move and overrides' in windows_gate
    assert 'PASS: packed service initialized and survived 20s' in windows_gate
    assert 'PASS latest native packed startup path' in windows_gate
    assert receipt['xs_commit'] == native_receipt['revision'] == json.loads((ROOT / 'deps.lock').read_text())['xserver']['commit']
    badging = (HERE / 'apk-badging.txt').read_text(encoding='utf-8')
    assert "package: name='org.xleaves.mdo.qa.stability' versionCode='30000047'" in badging
    assert 'application-debuggable' in badging
    assert "minSdkVersion:'26'" in badging and "targetSdkVersion:'36'" in badging
    assert "application-label-zh:'墨斗'" in badging
    assert "application-label-en:'mdo'" in badging and "application-label-ru:'mdo'" in badging
    log = (HERE / 'apk-build.txt').read_text(encoding='utf-8')
    assert 'Verified using v2 scheme (APK Signature Scheme v2): true' in log
    assert 'Verified using v3 scheme (APK Signature Scheme v3): true' in log
    assert '[apk] zipalign -c -P' in log
    helper = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
    with tempfile.TemporaryDirectory(prefix='mdo-stability-artifact-') as raw:
        directory = Path(raw)
        pack = directory / 'app.xrtpack'
        helper['extract_pack'](args.exe, pack)
        with zipfile.ZipFile(args.apk) as apk:
            assert apk.testzip() is None
            names = apk.namelist()
            assert len(names) == len(set(names))
            assert apk.read('assets/app.xrtpack') == pack.read_bytes()
            metadata = json.loads(apk.read('assets/mdo-runtime.json'))
            assert metadata['edition'] == 'lite' and not metadata['files']
            assert int(metadata['build_id']) == 30000047
            assert {name for name in names if name.startswith('lib/')} == {'lib/arm64-v8a/libxs.so'}
            native = apk.read('lib/arm64-v8a/libxs.so')
            assert sha(native) == native_receipt['binary_sha256'] and len(native) == native_receipt['binary_bytes']
            assert not elf_dependencies(native, 'libxs.so') - SYSTEM_LIBRARIES
            info = apk.getinfo('lib/arm64-v8a/libxs.so')
            assert info.compress_type == zipfile.ZIP_STORED
            with args.apk.open('rb') as stream:
                stream.seek(info.header_offset)
                header = stream.read(30)
            name_size, extra_size = struct.unpack_from('<HH', header, 26)
            assert (info.header_offset + 30 + name_size + extra_size) % 16384 == 0
            assert native[:5] == b'\x7fELF\x02'
            phoff = struct.unpack_from('<Q', native, 32)[0]
            phsize, phcount = struct.unpack_from('<HH', native, 54)
            loads = [phoff + i * phsize for i in range(phcount)
                if struct.unpack_from('<I', native, phoff + i * phsize)[0] == 1]
            assert loads and all(struct.unpack_from('<Q', native, offset + 48)[0] >= 16384 for offset in loads)
            assert any(b'Lorg/xserver/android/PageLocation;' in apk.read(name) for name in names if name.endswith('.dex'))
            library = directory / 'libxs.so'
            library.write_bytes(native)
            library.with_name('libxs.so.build.json').write_text(json.dumps(native_receipt), encoding='utf-8')
            build_mdo.verify_host_receipt(library, json.loads((ROOT / 'deps.lock').read_text()), False)
        report = dict(passed=True, mdo_source_commit=receipt['mdo_commit'], xs_commit=native_receipt['revision'],
            windows=dict(path=str(ROOT / 'mdo.exe'), bytes=len(exe), sha256=sha(exe), workspace_updated=True,
                installation_path=installed['path'], installation_updated=True,
                native_portable_startup_passed=True, packed_startup_seconds=20,
                system_clipboard_tested=False, actual_export_save_tested=False),
            android=dict(path=str(args.apk), bytes=args.apk.stat().st_size, sha256=sha(args.apk.read_bytes()),
                package='org.xleaves.mdo.qa.stability', edition='lite', build_id=30000047,
                signature_v2=True, signature_v3=True, native_zip_and_elf_alignment_16k=True,
                page_resolver_in_dex=True, native_sha256=sha(native), exact_pack_from_windows=True,
                installed=False, webview_tested=False), pack_sha256=sha(pack.read_bytes()),
            website_published=False, stress_or_high_load=False)
    (HERE / 'artifacts.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS workspace/installed executable, native startup, isolated ARM64 APK, signatures, 16 KB alignment and exact VFS/SDK')


if __name__ == '__main__':
    main()
