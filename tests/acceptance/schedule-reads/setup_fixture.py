"""Create one future, local-model schedule in a disposable QA Home."""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from test_api_runtime import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    root = parser.parse_args().directory.resolve()
    state = json.loads((root / 'state.json').read_text(encoding='utf-8'))
    definition = dict(id='qa-schedule-read', label='读取恢复验收',
        input='SCHEDULE_READ_RUN', project_id='default', agent_id='mdo.default',
        model_id='conversation-fixture', frequency='once',
        start_at=int((time.time() + 86400) * 1000000))
    status, _, raw = request(state['upstream_port'], 'POST', '/api/v1/schedules',
        body=json.dumps(definition).encode(), headers={'Content-Type': 'application/json'})
    assert status == 201, (status, raw)
    (root / 'schedule.json').write_bytes(raw)
    print(json.loads(raw)['data']['id'])


if __name__ == '__main__':
    main()
