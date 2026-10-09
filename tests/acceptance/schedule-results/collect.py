"""Collect a bounded packed schedule run; browser actions remain in CUA.

Use only a disposable manual_conversation_retry_qa.py fixture. The setup
copies one unchanged legacy line from the isolated baseline, before a run.
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from test_api_runtime import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--stage', choices=['setup', 'failure', 'restart', 'success', 'final'], required=True)
    args = parser.parse_args()
    base = args.fixture.resolve()
    state = json.loads((base / 'state.json').read_text(encoding='utf-8'))
    port = state['port']

    def get(path):
        status, _, raw = request(port, 'GET', path)
        assert status == 200, (path, status, raw)
        return raw

    if args.stage == 'setup':
        assert args.baseline is not None
        body = dict(id='qa-schedule-quota', label='计划额度不足验收', input='quota',
            project_id='default', agent_id='mdo.default', model_id='conversation-fixture',
            reasoning_effort='none', start_at=int((time.time() + 3600) * 1000000), frequency='once')
        status, _, raw = request(port, 'POST', '/api/v1/schedules', body=json.dumps(body).encode(),
            headers={'Content-Type': 'application/json'})
        assert status == 201, raw
        (HERE / 'native-created.json').write_bytes(raw)
        legacy = args.baseline / 'home/schedules/history/qa-schedule-quota.jsonl'
        target = base / 'home/schedules/history/qa-schedule-quota.jsonl'
        assert not target.exists()
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(legacy, target)
        (HERE / 'legacy.jsonl').write_bytes(legacy.read_bytes())
    (HERE / f'{args.stage}-history.json').write_bytes(get('/api/v1/schedules/qa-schedule-quota/history'))
    (HERE / f'{args.stage}-catalog.json').write_bytes(get('/api/v1/schedules'))
    for source, name in (('calls.json', f'{args.stage}-calls.json'),
                         ('requests.json', f'{args.stage}-requests.json'),
                         ('state.json', f'{args.stage}-state.json')):
        if (base / source).exists():
            (HERE / name).write_bytes((base / source).read_bytes())
    if args.stage == 'final':
        (HERE / 'final-history.jsonl').write_bytes((base / 'home/schedules/history/qa-schedule-quota.jsonl').read_bytes())
        prefix = f'/api/v1/projects/default/sessions/{state["session"]}'
        for path, name in ((prefix + '/history', 'final-session-history.json'),
                           (prefix + '/events?limit=32', 'final-events.json')):
            (HERE / name).write_bytes(get(path))
    print(f'Collected {args.stage} native history and catalog')


if __name__ == '__main__':
    main()
