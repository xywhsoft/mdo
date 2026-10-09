"""Bind interruption/profile evidence to the two actual packed candidates."""
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


def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--legacy-exe', type=Path, required=True)
    parser.add_argument('--apk', type=Path, required=True)
    args = parser.parse_args()
    baseline = read('legacy-switch-baseline.json')['checks'][3]
    fixed = read('legacy-fixed.json')
    assert baseline['model_switch_status'] == 409 and baseline['final_history_status'] == 422
    assert fixed['checks'][3]['model_switch_status'] == fixed['checks'][3]['final_history_status'] == 200
    assert fixed['checks'][3]['resume_required'] and fixed['checks'][3]['pending_calls'] == 2
    assert fixed['original_files_content_unchanged'] and fixed['isolated_copy_only']
    assert fixed['model_generation_requests'] == fixed['tools_executed'] == fixed['resume_requests'] == 0
    active = [item for item in fixed['checks'] if item['state'] == 'active']
    assert len(active) == 6 and all(item['final_history_status'] == 200 for item in active)
    assert fixed['candidate_sha256'] == sha(args.legacy_exe.read_bytes())
    assert not fixed['raw_user_data_in_report'] and not fixed['account_credentials_copied']
    assert 'AssertionError: (409,' in (HERE / 'baseline.txt').read_text(encoding='utf-8')
    for result in read('runtime.json')['checks']:
        assert result['result'] == 'PASS' and result['completed_tools'] == 1
        assert result['provider_requests'] == result['tools_executed_by_model_changes'] == 0
        assert result['pending_sequence_unchanged_by_model_changes']
        assert result['invalid_model_rejected_without_metadata_change']
        assert result['missing_model_recovery_code'] == 'model_profile_model_missing'
    ui = read('packed-ui.json')
    assert ui['calls'] == {'streaming': 2, 'continue': 1}
    assert ui['final_errors'] == 0 and ui['successful_runs'] == 2 and not ui['resume_required']
    assert sorted(ui['run_states']) == ['cancelled', 'succeeded', 'succeeded']
    assert not ui['manual_read_after_selection'] and ui['continue_and_followup_in_browser']
    assert not read('browser.json')['warn_or_error_logs']
    assert 'pass 982' in (HERE / 'frontend.txt').read_text(encoding='utf-8')
    assert 'Ran 142 tests' in (HERE / 'contracts.txt').read_text(encoding='utf-8')
    assert 'API runtime probe: PASS' in (HERE / 'api.txt').read_text(encoding='utf-8')
    receipt = read('candidate.json')
    exe = args.exe.read_bytes()
    assert receipt['mdo_commit'].startswith('f5e55072') and not receipt['mdo_worktree_dirty']
    assert sha(exe) == receipt['sha256'] and len(exe) == receipt['bytes']
    assert (ROOT / '.build/conversation-acceptance/interrupted-profile/ui-final/run/mdo.exe').read_bytes() == exe
    installed = read('install-update.json')
    for item in installed['updates']:
        assert Path(item['path']).read_bytes() == exe
        assert sha(Path(item['backup']).read_bytes()) == item['previous_sha256']
        assert item['home_inventory_unchanged'] and not item['app_launched']
    helper = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
    native_receipt = json.loads((HERE.parent / 'stability-final/native.json').read_text())
    with tempfile.TemporaryDirectory(prefix='mdo-profile-evidence-') as raw:
        folder = Path(raw)
        pack = folder / 'app.xrtpack'
        helper['extract_pack'](args.exe, pack)
        files = helper['files_from_pack'](pack.read_bytes())
        sources = ('src/api/recovery.c', 'src/sessions/manager.c',
            'web/js/app.js', 'web/js/features/chat/composer-profile.js')
        assert all(files[name] == (ROOT / 'app' / name).read_bytes() for name in sources)
        with zipfile.ZipFile(args.apk) as apk:
            assert apk.testzip() is None and apk.read('assets/app.xrtpack') == pack.read_bytes()
            native = apk.read('lib/arm64-v8a/libxs.so')
            assert sha(native) == native_receipt['binary_sha256']
            assert not elf_dependencies(native, 'libxs.so') - SYSTEM_LIBRARIES
            metadata = json.loads(apk.read('assets/mdo-runtime.json'))
            assert metadata['edition'] == 'lite' and not metadata['files']
            info = apk.getinfo('lib/arm64-v8a/libxs.so')
            assert info.compress_type == zipfile.ZIP_STORED
            with args.apk.open('rb') as stream:
                stream.seek(info.header_offset)
                header = stream.read(30)
            name_size, extra_size = struct.unpack_from('<HH', header, 26)
            assert (info.header_offset + 30 + name_size + extra_size) % 16384 == 0
            library = folder / 'libxs.so'
            library.write_bytes(native)
            library.with_name('libxs.so.build.json').write_text(json.dumps(native_receipt))
            build_mdo.verify_host_receipt(library, json.loads((ROOT / 'deps.lock').read_text()), False)
        pack_sha = sha(pack.read_bytes())
    badging = (HERE / 'apk-badging.txt').read_text(encoding='utf-8')
    assert "package: name='org.xleaves.mdo.qa.stability' versionCode='30000047'" in badging
    assert 'application-debuggable' in badging and "targetSdkVersion:'36'" in badging
    apk_log = (HERE / 'apk-build.txt').read_text(encoding='utf-8')
    assert 'Verified using v2 scheme (APK Signature Scheme v2): true' in apk_log
    assert 'Verified using v3 scheme (APK Signature Scheme v3): true' in apk_log
    report = {'passed': True, 'mdo_source_commit': receipt['mdo_commit'],
        'windows_bytes': len(exe), 'windows_sha256': sha(exe), 'pack_sha256': pack_sha,
        'embedded_production_files_equal': len(sources), 'legacy_sessions_read': len(active),
        'personal_history_sent_to_model': False, 'original_files_content_unchanged': True,
        'apk_bytes': args.apk.stat().st_size, 'apk_sha256': sha(args.apk.read_bytes()),
        'apk_exact_windows_pack': True, 'phone_installed_or_tested': False,
        'website_published': False, 'stress_or_high_load': False}
    (HERE / 'checks.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS interrupted model selection, real old-data copy, packed UI continuation, installations and isolated APK')


if __name__ == '__main__':
    main()
