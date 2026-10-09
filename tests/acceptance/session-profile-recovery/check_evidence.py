"""Check real packed model settings, typed final feedback and source provenance."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ('web/js/api/request-recovery.js', 'web/js/state/session-profile.js',
    'web/js/state/sessions.js', 'web/js/features/chat/composer-profile.js',
    'web/js/utils/dom.js', 'web/lang/zh-CN.json', 'web/lang/en-US.json', 'web/lang/ru-RU.json')


def load(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def candidate(exe):
    spec = importlib.util.spec_from_file_location('profile_pack_reader',
        HERE.parent / 'decision-write-recovery/check_candidate.py')
    reader = importlib.util.module_from_spec(spec); spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name('build.json').read_text(encoding='utf-8'))
    sha = lambda raw: hashlib.sha256(raw).hexdigest()
    raw = exe.read_bytes()
    assert receipt['sha256'] == sha(raw) and receipt['bytes'] == len(raw)
    assert not receipt['mdo_worktree_dirty']
    assert receipt['xs_commit'] == json.loads((ROOT / 'deps.lock').read_text())['xserver']['commit']
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / 'app.xrtpack'
        reader.extract_pack(exe, archive)
        pack = archive.read_bytes(); entries = reader.files_from_pack(pack)
    sources = {}
    for name in FILES:
        expected = (ROOT / 'app' / name).read_bytes()
        assert entries[name] == expected, name
        sources[name] = {'sha256': sha(expected), 'matches': True}
    assert not any('manual_session_history_qa' in name for name in entries)
    report = {key: receipt[key] for key in ('mdo_commit', 'xs_commit',
        'mdo_worktree_dirty', 'sha256', 'bytes', 'packaged_app_sha256')}
    report.update(pack_sha256=sha(pack), sources=sources, passed=True,
        daily_app_overwritten=False, android_device_tested=False, external_provider_tested=False)
    (HERE / 'candidate.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    if args.exe:
        candidate(args.exe.resolve())
    for name, statuses, revision in (('baseline.json', [200], 2), ('drop.json', [200], 2),
        ('refusal.json', [200, 412], 2), ('route.json', [200, 200], 3), ('wire.json', [200], 2)):
        proof = load(name)
        writes = [entry for entry in proof['transport'] if entry['kind'] == 'profile']
        assert [entry['status'] for entry in writes] == statuses, name
        assert proof['metadata_before']['revision'] == 1
        assert proof['metadata_after']['revision'] == revision
        assert proof['metadata_after']['model_id'] == 'profile-alternative'
        assert proof['metadata_after']['id'] == proof['source_id']
        assert not proof['forks'] and not proof['final_errors']
        assert writes[0]['response']['data']['reasoning_effort'] == 'medium'
        assert writes[0]['body']['model_id'] == 'profile-alternative'
        if name == 'route.json':
            assert proof['source_snapshot_unchanged'] and proof['model_calls'] == 1
            assert writes[0]['fault'] == 'stall_body'
            assert proof['metadata_after']['permission_profile'] == 'read-only'
            assert writes[1]['body']['permission_profile'] == 'read-only'
        elif name == 'baseline.json':
            assert proof['source_snapshot_unchanged'] and proof['model_calls'] == 1
        else:
            assert writes[0]['fault'] == 'drop_ack' and proof['model_calls'] == 2
            assert proof['event_kinds']['model_done'] == proof['event_kinds']['agent_done'] == 2
        if name == 'refusal.json':
            assert writes[1]['body'] == writes[0]['body']
            assert writes[1]['response']['error']['code'] == 'revision_conflict'
            assert sum(entry['kind'] == 'profile_read' and entry['status'] == 503
                for entry in proof['transport']) >= 5
        if name == 'wire.json':
            assert proof['model_requests'] == ['ornith-1.5-35b', 'profile-alternative']
    ui = load('ui-observations.json')
    assert ui['baseline']['notices'] == ['无法连接本地服务，请确认 mdo 仍在运行。']
    assert ui['baseline']['model'] == 'queue-fixture'
    for name in ('recovery', 'wire_model'):
        assert not ui[name]['intermediate']['notices'] and not ui[name]['confirmed']['notices']
        assert ui[name]['confirmed']['model'] == 'profile-alternative'
        assert not ui[name]['confirmed']['disabled']
        assert 'Seed the history recovery check.' in ui[name]['continuation']['timeline']
        assert not ui[name]['continuation']['notices']
    refusal = ui['refusal']
    assert not refusal['intermediate']['notices'] and refusal['final_seen']
    assert refusal['final']['notices'] == ['会话配置暂未确认，请恢复连接后检查，或重新选择配置。']
    assert not refusal['final']['disabled']
    assert refusal['final']['draft'] == refusal['manual_recovered']['draft']
    assert refusal['manual_recovered']['model'] == 'profile-alternative'
    assert not refusal['manual_recovered']['notices'] and not refusal['continuation']['notices']
    assert ui['route']['cancelled'] == {'heading': '设置', 'notices': [], 'openDialogs': 0}
    assert ui['route']['returned']['draft'] == 'Keep model settings route draft.'
    assert not ui['route']['returned']['disabled']
    assert ui['route']['permission']['permission'] == 'read-only'
    build = load('candidate.json')
    assert build['passed'] and not build['mdo_worktree_dirty']
    assert all(build['sources'][name]['matches'] for name in FILES)
    print('PASS real packed model/permission recovery, one final notice, wire model and eight VFS files')


if __name__ == '__main__':
    main()
