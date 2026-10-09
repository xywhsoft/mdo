"""Verify recorded fork ownership, bounded recovery and optional packed VFS bytes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ('include/mdo/sessions.h', 'src/sessions/manager.c', 'src/api/sessions.c',
    'web/js/state/session-fork.js', 'web/js/state/sessions.js', 'web/js/app.js',
    'web/js/utils/dom.js', 'web/lang/zh-CN.json', 'web/lang/en-US.json',
    'web/lang/ru-RU.json')


def load(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def candidate(exe):
    # Use the existing CRC/size-bounded reader; this test has no product fault seam.
    spec = importlib.util.spec_from_file_location('fork_pack_reader',
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


def writes(proof):
    return [entry for entry in proof['transport'] if entry['kind'] == 'fork']


def reads(proof):
    return [entry for entry in proof['transport'] if entry['kind'] == 'child_read']


def delays(entries):
    for index, (first, second) in enumerate(zip(entries, entries[1:])):
        delay = second['seconds'] - first['seconds']
        base = .5 * 2 ** index
        assert base - .05 <= delay <= base * 1.2 + .3, (index, delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    if args.exe:
        candidate(args.exe.resolve())
    baseline = load('baseline-ui.json')
    assert len(writes(baseline)) == len(baseline['forks']) == 2
    assert writes(baseline)[0]['fault'] == 'drop_ack'
    assert len({entry['id'] for entry in baseline['forks']}) == 2
    assert baseline['source_snapshot_unchanged'] and not baseline['final_errors']
    for name in ('drop.json', 'cancel-before-sidebar.json', 'cancel.json',
            'refusal.json', 'route-cancel.json'):
        proof = load(name)
        submitted = writes(proof)
        assert len(submitted) == len(proof['forks']) == 1, name
        child = proof['forks'][0]
        assert child['parent_session_id'] == proof['source_id']
        assert child['forked_through_sequence'] == submitted[0]['body']['through_sequence'] == 3
        assert child['id'] == submitted[0]['body']['client_session_id']
        assert submitted[0]['status'] == 201
        assert proof['source_snapshot_unchanged'] and not proof['final_errors']
        assert all(entry['path'].endswith('/' + child['id']) for entry in reads(proof))
        if name in ('cancel-before-sidebar.json', 'cancel.json', 'route-cancel.json'):
            assert submitted[0]['fault'] == 'stall_body'
            assert submitted[0]['finished_seconds'] < reads(proof)[0]['seconds']
        elif name == 'drop.json':
            assert submitted[0]['fault'] == 'drop_ack'
            assert reads(proof)[0]['status'] == 200
            assert proof['model_calls'] == 2
        else:
            confirmation = reads(proof)
            assert [entry['status'] for entry in confirmation[:11]] == [503] * 11
            assert all(entry['status'] == 200 for entry in confirmation[11:])
            # First intent: one POST + five reads. Explicit retry: six reads,
            # even while the service remains unavailable. Both have one budget.
            delays(submitted + confirmation[:5]); delays(confirmation[5:11])
            assert proof['model_calls'] == 2
    native = load('native.json'); old_native = load('baseline-native.json')
    assert native['passed'] and len(native['cases']) == 7
    assert native['source_snapshot_unchanged'] and native['model_calls'] == 1
    assert old_native['passed'] and len(set(old_native['cases'][0]['ids'])) == 2
    ui = load('ui-observations.json')
    assert ui['recovery']['source_draft'] == 'Fork ACK draft must stay.'
    assert not ui['recovery']['intermediate_error_visible']
    for name in ('cancel_before_sidebar_fix', 'cancel_after_sidebar_fix', 'route_cancel'):
        assert not ui[name]['notices'] and not ui[name]['openDialogs']
    assert ui['cancel_after_sidebar_fix']['branchVisible']
    assert ui['cancel_after_sidebar_fix']['draft'] == 'Keep draft when fork confirmation is cancelled.'
    assert ui['route_cancel']['heading'] == '设置'
    assert ui['route_cancel']['source_draft'] == 'Keep route-change draft.'
    refusal = ui['refusal']
    assert not refusal['intermediate']['notices']
    assert refusal['first_final_notice_seen']
    assert refusal['second_final']['notices'] == ['分支创建结果暂未确认。请重试，或在会话列表查看分支。']
    assert refusal['second_final']['branchVisible'] and refusal['second_final']['forkButtonEnabled']
    assert refusal['source_draft_after_continuation'] == 'Retain the draft after a final fork refusal.'
    assert not refusal['continuation']['notices']
    assert 'Continue after the recovered final fork notice.' in refusal['continuation']['messages'][0]
    build = load('candidate.json')
    assert build['passed'] and not build['mdo_worktree_dirty']
    assert all(build['sources'][name]['matches'] for name in FILES)
    print('PASS native replay, real browser recovery/cancellation/final notice and ten packed VFS files')


if __name__ == '__main__':
    main()
