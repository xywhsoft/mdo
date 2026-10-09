"""Recheck saved proof, without making new model or HTTP requests."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
baseline = json.loads((HERE / 'baseline.json').read_text())
assert not baseline['passed']
assert baseline['cases'][0]['statuses'] == [200, 404]
native = json.loads((HERE / 'native.json').read_text())
assert native['passed']
cases = {case['kind']: case for case in native['cases']}
assert cases['cache_boundaries']['checks'] == 151
assert cases['concurrent_same_answer']['statuses'] == [200, 200]
assert cases['interactive_replay_after_completion']['exec_invocations'] == 1
assert cases['interactive_replay_after_completion']['final_errors'] == 0
assert cases['task_answer_replay_after_completion']['replayed']
assert not cases['cancelled_question']['falsely_acknowledged']
assert cases['restart']['old_token_rejected'] and cases['restart']['receipt_cleared']
browser = json.loads((HERE / 'browser.json').read_text())
assert sum(browser['model_calls'].values()) == 7
assert browser['exec_invocations'] == 1 and browser['final_errors'] == 0
assert len(browser['tool_results']) == 4 and all(e['success'] for e in browser['tool_results'])
groups = {}
for attempt in browser['transport']:
    groups.setdefault(attempt['path'], []).append(attempt)
first_answer, approval, refused, next_answer = groups.values()
for group in (first_answer, approval, next_answer):
    assert len(group) == 2 and all(e['native_status'] == 200 for e in group)
    assert group[0]['body'] == group[1]['body']
assert first_answer[0]['fault'] == next_answer[0]['fault'] == 'drop_ack'
assert approval[0]['fault'] == 'stall_body'
assert .5 <= first_answer[1]['seconds'] - first_answer[0]['seconds'] < 2
assert 8 <= approval[1]['seconds'] - approval[0]['seconds'] < 11
assert len(refused) == 19
assert all(e['native_status'] is None and e['status'] == 503 for e in refused[:18])
assert refused[-1]['native_status'] == 200
assert all(e['body'] == refused[0]['body'] for e in refused)
for start in (0, 6, 12):
    group = refused[start:start + 6]
    assert 15 <= group[-1]['seconds'] - group[0]['seconds'] < 20
candidate = json.loads((HERE / 'candidate.json').read_text())
assert candidate['passed'] and not candidate['mdo_worktree_dirty']
assert len(candidate['sources']) == 15 and all(v['matches'] for v in candidate['sources'].values())
print('PASS saved baseline, native boundaries, quiet browser recovery and exact candidate')
