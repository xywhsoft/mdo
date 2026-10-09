"""Check recorded packed-browser transport, ownership and optional VFS bytes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ('web/js/state/sessions.js', 'web/js/features/sessions/session-history-read.js',
    'web/js/app.js', 'web/js/features/chat/timeline.js',
    'web/js/features/sessions/session-action-menu.js',
    'web/js/features/sessions/session-list.js', 'web/js/features/chat/slash-commands.js')


def load(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def candidate(exe):
    # Reuse the CRC/size-bounded XRTPACK reader from the preceding acceptance.
    spec = importlib.util.spec_from_file_location('decision_pack_reader',
        HERE.parent / 'decision-write-recovery/check_candidate.py')
    reader = importlib.util.module_from_spec(spec); spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name('build.json').read_text(encoding='utf-8'))
    sha = lambda data: hashlib.sha256(data).hexdigest()
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
    report = {key: receipt[key] for key in ('mdo_commit', 'xs_commit', 'mdo_worktree_dirty',
        'sha256', 'bytes', 'packaged_app_sha256')}
    report.update(pack_sha256=sha(pack), sources=sources, passed=True,
        daily_app_overwritten=False, android_device_tested=False, external_provider_tested=False)
    (HERE / 'candidate.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    if args.exe:
        candidate(args.exe.resolve())
    baseline = load('baseline.json')
    assert [item['status'] for item in baseline['transport']] == [503]
    assert baseline['model_calls'] == 1 and not baseline['forks']
    for name, statuses, calls in [('recover.json', [503, 503, 200], 2),
            ('cancel.json', [200, 200], 1), ('refusal.json', [503] * 6 + [200], 2)]:
        evidence = load(name)
        reads = [item for item in evidence['transport'] if item['method'] == 'GET']
        writes = [item for item in evidence['transport'] if item['method'] == 'POST']
        assert [item['status'] for item in reads] == statuses, name
        assert len(writes) == len(evidence['forks']) == 1, name
        assert evidence['model_calls'] == calls, name
        history, fork = reads[-1]['response']['data'], writes[0]['response']['data']
        assert history['session_id'] == fork['parent_session_id'] == evidence['source_id']
        assert history['project_id'] == fork['project_id'] == 'default'
        assert history['last_sequence'] == writes[0]['body']['through_sequence'] == fork['forked_through_sequence'] == 3
        assert writes[0]['status'] == 201 and fork['id'] == evidence['forks'][0]['id']
        assert len(set(item['path'] for item in reads)) == 1
        assert evidence['source_snapshot_unchanged'] and evidence['final_errors'] == 0
        if name == 'cancel.json':
            assert reads[0]['fault'] == 'stall_body'
            assert reads[0]['finished_seconds'] < reads[1]['seconds'] < writes[0]['seconds']
        else:
            for i, (first, second) in enumerate(zip(reads[:6], reads[1:6])):
                delay = second['seconds'] - first['seconds']
                base = .5 * 2 ** i
                assert base - .05 <= delay <= base * 1.2 + .3, (name, i, delay)
    assert baseline['source_snapshot_unchanged'] and baseline['final_errors'] == 0
    ui = load('ui-observations.json')
    assert ui['recovery']['source_draft'] == 'History draft must stay.'
    assert not ui['recovery']['intermediate_error_visible']
    assert not ui['cancel']['notices'] and not ui['cancel']['openDialogs']
    assert ui['refusal']['notices'] == ['暂时无法读取会话历史，请稍后重试。']
    assert ui['refusal']['draft'] == 'Retain the draft after a final history refusal.'
    assert ui['refusal']['forkButtonEnabled']
    build = load('candidate.json')
    assert build['passed'] and not build['mdo_worktree_dirty']
    assert all(build['sources'][name]['matches'] for name in FILES)
    print('PASS packed baseline, quiet recovery, route cancellation, one final notice and seven VFS files')


if __name__ == '__main__':
    main()
