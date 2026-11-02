"""Offline end-to-end history fault isolation through the packed HTTP API.

Only small disposable fixtures are written. No user sessions, accounts or
model endpoints are accessed. --pause-for-ui keeps that fixture available
until Enter is received, for inspecting the real chat renderer.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from test_packed_home_lease import site, start, stop, request, wait_bootstrap
from test_conversation_groups_runtime import prepare_history_home
from test_conversation_faults_native import record, line

ROOT = Path(__file__).resolve().parents[1]


def fixture(session, name):
    def add(ident, kind, text=''):
        item = record(session, ident, kind, text)
        item['project_id'] = 'default'
        return line(item)
    if name == 'bad-middle':
        return (add(1, 0, '请整理 Excel，并展示处理结果。') + add(2, 2, '正常内容：已识别原始表格和字段。') +
            b'{damaged record\n' + add(3, 2, '正常内容：汇总结果仍然可以查看。') + add(4, 10))
    if name == 'all-bad':
        return b'{broken\nnot json\n'
    if name == 'bad-suffix':
        return add(1, 0, 'healthy question') + add(2, 2, 'healthy prefix') + (b'{' + b' '*1022 + b'\n')*(18*1024)
    if name == 'long-text':
        return (add(1, 0, 'long reply') + b''.join(add(n+2, 2, 'x'*60000) for n in range(20)) +
            add(22, 4) + add(23, 10))
    if name == 'bad-attachment':
        return add(1, 0, 'question with damaged attachment metadata') + add(2, 2, 'healthy reply') + add(3, 10)
    if name == 'oversized-record':
        return (add(1, 0, 'question') + add(2, 2, 'healthy before') + add(3, 2, 'x'*200000) +
            add(4, 2, 'healthy after') + add(5, 10))
    raise AssertionError(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--pause-for-ui', action='store_true')
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix='history-fault-api-', dir=ROOT/'.build') as raw:
        base = Path(raw)
        assert base.resolve().is_relative_to((ROOT/'.build').resolve())
        path, port = site(base, 'site', packed)
        home = base/'home'; prepare_history_home(home)
        env = dict(os.environ, USERPROFILE=str(base), MDO_ORNITH_RESPONSES_URL='http://127.0.0.1:1/v1')
        process = start(path, packed, home, env)
        sessions = {}
        try:
            _, boot = wait_bootstrap(process, port, path/'packed.log')
            assert boot['data']['ready'], boot
            for name in ['bad-middle','all-bad','bad-suffix','long-text','bad-attachment','oversized-record']:
                status, doc = request(port, 'POST', '/api/v1/sessions', dict(project_id='default',
                    title=name, agent_id='mdo.default', model_id='history-fixture',
                    protocol='openai-responses', reasoning_effort='medium', max_output_tokens=1024))
                assert status == 201, doc
                sessions[name] = doc['data']['id']
            stop(process); process = None
            originals = {}
            for name, session in sessions.items():
                journal = home/'sessions/default'/session/'ui-events.jsonl'
                journal.write_bytes(fixture(session, name))
                if name == 'bad-attachment':
                    metadata = journal.parent/'attachments/events/1.json'
                    metadata.parent.mkdir(parents=True, exist_ok=True)
                    metadata.write_text('{damaged attachment metadata',encoding='utf-8')
                originals[journal] = hashlib.sha256(journal.read_bytes()).hexdigest()
            process = start(path, packed, home, env)
            wait_bootstrap(process, port, path/'packed.log')
            pages = {}
            for name, session in sessions.items():
                endpoint = f'/api/v1/projects/default/sessions/{session}/conversation'
                query = '?limit=1'; seen = []; notices = []; previous = None
                for number in range(8):
                    status, doc = request(port, 'GET', endpoint+query)
                    assert status == 200, (name, doc)
                    page = doc['data']; rows = json.loads(page['items_json'])
                    assert hashlib.sha256(page['items_json'].encode()).hexdigest() == page['items_hash']
                    assert not page['history_lost'] and not page['delta'], (name, page)
                    assert len(page['items_json'].encode()) <= 4*1024*1024
                    assert len(page['notices']) <= 16
                    for notice in page['notices']:
                        assert notice['id'] == f"{page['epoch']}:{notice['offset']}:{notice['code']}"
                    ids = [item['event_id'] for item in rows]
                    assert ids == sorted(set(ids)) and not set(ids)&{item['event_id'] for item in seen}
                    seen.extend(rows); notices.extend(page['notices'])
                    if previous is not None: assert page['next_scan_before'] < previous
                    previous = page['next_scan_before']
                    if not page['has_more']: break
                    query = f"?limit=1&before={page['next_before']}&scan_before={previous}&epoch={page['epoch']}"
                else: raise AssertionError(f'{name}: paging failed to reach beginning')
                assert notices, name
                texts = [row['text'] for row in sorted(seen,key=lambda row:row['event_id']) if row['kind']=='model_text_delta']
                if name == 'bad-middle':
                    assert len(texts) == 2 and all(text.startswith('正常内容：') for text in texts), texts
                elif name == 'all-bad':assert not rows
                elif name == 'bad-suffix':assert texts == ['healthy prefix']
                elif name == 'bad-attachment':
                    assert texts == ['healthy reply'] and any(n['code']=='unreadable_event' for n in notices)
                    assert all('action_prompt' not in row for row in seen)
                elif name == 'oversized-record':assert texts == ['healthy before','healthy after']
                else:assert sum(map(len,texts)) == 1200000
                pages[name] = {'pages':number+1,'rows':len(seen),'notices':len(notices)}
                # Continuations belong to one file generation, never another session.
                status, _ = request(port, 'GET', endpoint+'?limit=1&scan_before=0&epoch='+'f'*64)
                assert status == 409
                status, _ = request(port, 'GET', endpoint+'?limit=1&scan_before=0')
                assert status == 400
                print(f'PASS {name}: {pages[name]}', flush=True)
            for journal, digest in originals.items():
                assert hashlib.sha256(journal.read_bytes()).hexdigest() == digest
            evidence = ROOT/'.build/acceptance/conversation-faults-20261102'
            evidence.mkdir(parents=True, exist_ok=True)
            (evidence/'api.json').write_text(json.dumps(pages,indent=2),encoding='utf-8')
            if args.pause_for_ui:
                print(f"UI_READY http://127.0.0.1:{port}/#/projects/default/sessions/{sessions['bad-middle']}",flush=True)
                input('Press Enter to stop the disposable UI fixture.\n')
        finally:
            if process:stop(process)
    print('PASS packed API partial history, malformed-only pages, continuation fencing and read-only journals')


if __name__ == '__main__':main()
