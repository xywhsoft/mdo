"""Verify exact native rejection, safe retry boundaries and the packed sources."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ('include/mdo/models.h', 'src/agents/runtime.c', 'src/api/internal.h',
    'src/api/profile.c', 'src/api/runs.c', 'src/api/sessions.c', 'src/models/catalog.c',
    'src/models/completion.inc.c', 'web/js/utils/model-errors.js',
    'web/lang/zh-CN.json', 'web/lang/en-US.json', 'web/lang/ru-RU.json')


def load(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def candidate(exe):
    spec = importlib.util.spec_from_file_location('profile_errors_pack',
        HERE.parent / 'decision-write-recovery/check_candidate.py')
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name('build.json').read_text(encoding='utf-8'))
    digest = lambda raw: hashlib.sha256(raw).hexdigest()
    raw = exe.read_bytes()
    assert receipt['sha256'] == digest(raw) and receipt['bytes'] == len(raw)
    assert not receipt['mdo_worktree_dirty']
    assert receipt['xs_commit'] == json.loads((ROOT / 'deps.lock').read_text())['xserver']['commit']
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / 'app.xrtpack'
        reader.extract_pack(exe, archive)
        entries = reader.files_from_pack(archive.read_bytes())
    sources = {}
    for name in FILES:
        expected = (ROOT / 'app' / name).read_bytes()
        assert entries[name] == expected, name
        sources[name] = {'sha256': digest(expected), 'matches': True}
    report = {key: receipt[key] for key in ('mdo_commit', 'xs_commit',
        'mdo_worktree_dirty', 'sha256', 'bytes', 'packaged_app_sha256')}
    report.update(sources=sources, passed=True, daily_app_overwritten=False,
        android_device_tested=False, external_provider_tested=False)
    (HERE / 'candidate.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    if args.exe:
        candidate(args.exe.resolve())
    native, ui = load('native.json'), load('browser-native.json')
    for proof in (native, ui):
        assert len(proof['rejections']) == 7
        assert [item['error']['code'] for item in proof['rejections']] == [
            'model_profile_model_missing', 'model_profile_protocol_unsupported',
            'model_profile_reasoning_unsupported', 'model_profile_output_limit',
            'model_profile_model_missing', 'model_profile_reasoning_unsupported',
            'model_profile_model_missing']
        assert proof['rejected_snapshot_unchanged'] and proof['final_errors'] == 0
        assert proof['model_calls'] == 2
        assert proof['model_requests'] == ['ornith-1.5-35b', 'profile-alternative']
        assert proof['metadata_before']['revision'] == 1 and proof['metadata_after']['revision'] == 2
        assert proof['event_kinds']['model_done'] == proof['event_kinds']['agent_done'] == 2
    writes = [item for item in ui['transport'] if item['kind'] == 'profile']
    assert [item['status'] for item in writes] == [422, 200]
    assert writes[0]['response']['error']['code'] == 'model_profile_reasoning_unsupported'
    assert writes[0]['body'] == {'model_id': 'profile-alternative',
        'reasoning_effort': 'medium', 'permission_profile': 'balanced'}
    assert writes[1]['body'] == {'model_id': 'profile-alternative',
        'reasoning_effort': 'none', 'permission_profile': 'balanced'}
    checkpoint = load('rejected-native.json')
    assert checkpoint['metadata'] == ui['metadata_before']
    assert checkpoint['model_done'] == 1 and checkpoint['errors'] == 0
    browser = load('ui-observations.json')
    draft = '保留这段输入，修正思考强度后继续对话。'
    assert browser['rejected']['draft'] == browser['confirmed']['draft'] == draft
    assert browser['rejected']['toasts'] == ['所选模型不支持此思考强度，请选择支持的强度。']
    assert browser['rejected']['selected'] == 'queue-fixture' and not browser['rejected']['disabled']
    assert browser['confirmed']['selected'] == 'profile-alternative' and not browser['confirmed']['disabled']
    assert browser['continued']['draft'] == '' and browser['continued']['errors'] == []
    assert draft in browser['continued']['timeline'] and 'Seed the profile error check.' in browser['continued']['timeline']
    assert browser['continued']['timeline'].count('Ready to continue.') == 2
    synchronized = load('synchronized-native.json')
    assert synchronized['model_calls'] == 1 and synchronized['final_errors'] == 0
    assert synchronized['metadata_after']['reasoning_effort'] == 'low'
    assert load('candidate.json')['passed']
    print('PASS real packed precise rejection, retained draft, continued wire model and twelve VFS files')


if __name__ == '__main__':
    main()
