"""Bounded real xs/TCC fork identity, replay, conflict and restart checks."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import tempfile
import threading

from manual_session_history_qa import Model
from test_api_runtime import request
from test_interrupt_runtime import until
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies
from test_packed_queue_start_recovery import configure_model


def check(packed, baseline):
    with tempfile.TemporaryDirectory(prefix='packed-fork-', dir=ROOT / '.build') as directory:
        base = Path(directory)
        native, port = site(base, 'native', packed)
        model = ThreadingHTTPServer(('127.0.0.1', 0), Model); Model.calls = 0
        worker = threading.Thread(target=model.serve_forever, daemon=True); worker.start()
        env = dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base), MDO_QUEUE_FIXTURE_KEY='fixture-only')
        home = base / 'home'
        process = start(native, packed, home, env)
        evidence = []

        def call(method, path, body=None, headers=None, expected=200):
            status, metadata, raw = request(port, method, '/api/v1' + path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={'Content-Type': 'application/json', **(headers or {})})
            result = json.loads(raw)
            assert status == expected, (status, result)
            return result.get('data'), metadata

        try:
            assert wait_bootstrap(process, port, native / 'packed.log')[1]['data']['ready']
            configure_model(port, f'http://127.0.0.1:{model.server_port}/v1')
            profile = {'project_id': 'default', 'model_id': 'queue-fixture',
                'reasoning_effort': 'none', 'permission_profile': 'balanced', 'workspace_root': str(base)}
            source, _ = call('POST', '/sessions', {**profile, 'title': 'Fork source'}, expected=201)
            path = '/projects/default/sessions/' + source['id']
            run, _ = call('POST', path + '/runs', {'prompt': 'Seed the fork check.'}, expected=202)
            result = until(lambda: (data if (data := call('GET', '/runs/' + run['id'])[0])['terminal'] else None), seconds=15)
            assert result['state'] == 'succeeded', result
            _, headers = call('GET', path)
            tag = headers['etag']; write_token = headers['x-mdo-write-token']
            snapshot = home / 'sessions/default' / source['id'] / 'snapshot.json'
            before = hashlib.sha256(snapshot.read_bytes()).hexdigest()
            body = {'title': 'Stable branch', 'through_sequence': 3, 'client_session_id': 'a' * 32}
            if baseline:
                call('POST', path + '/fork', body, {'If-Match': tag}, 422)
                legacy = {key: value for key, value in body.items() if key != 'client_session_id'}
                first, _ = call('POST', path + '/fork', legacy, {'If-Match': tag}, 201)
                second, _ = call('POST', path + '/fork', legacy, {'If-Match': tag}, 201)
                assert first['id'] != second['id']
                evidence.append({'case': 'same_legacy_post_creates_two_children', 'ids': [first['id'], second['id']]})
            else:
                first, _ = call('POST', path + '/fork', body, {'If-Match': tag}, 201)
                assert first['id'] == body['client_session_id'] and first['parent_session_id'] == source['id']
                target = home / 'sessions/default' / first['id'] / 'snapshot.json'
                target_hash = hashlib.sha256(target.read_bytes()).hexdigest()
                second, _ = call('POST', path + '/fork', body, {'If-Match': tag}, 200)
                assert second['id'] == first['id'] and target_hash == hashlib.sha256(target.read_bytes()).hexdigest()
                evidence.append({'case': 'repeat_same_child', 'statuses': [201, 200], 'id': first['id']})
                for patch in [{'title': 'Different'}, {'through_sequence': 2}]:
                    call('POST', path + '/fork', {**body, **patch}, {'If-Match': tag}, 409)
                evidence.append({'case': 'different_title_or_boundary_rejected', 'status': 409})
                for invalid in ['A' * 32, '../escape', 'short']:
                    call('POST', path + '/fork', {**body, 'client_session_id': invalid}, {'If-Match': tag}, 422)
                call('POST', path + '/fork', {'client_session_id': 'e' * 32}, {'If-Match': tag}, 422)
                call('POST', path + '/fork', body, {'If-Match': '"mdo-session-other-1"'}, 412)
                evidence.append({'case': 'invalid_identity_tail_or_owner_rejected', 'statuses': [422, 412]})
                collision, _ = call('POST', '/sessions', {**profile, 'title': 'Existing ordinary session',
                    'client_session_id': 'c' * 32}, expected=201)
                collision_meta = home / 'sessions/default' / collision['id'] / 'meta.json'
                collision_before = collision_meta.read_bytes()
                call('POST', path + '/fork', {**body, 'client_session_id': 'c' * 32}, {'If-Match': tag}, 409)
                assert collision_meta.read_bytes() == collision_before
                incomplete = home / 'sessions/default' / ('d' * 32)
                incomplete.mkdir(); (incomplete / 'marker').write_bytes(b'preserve incomplete folder')
                call('POST', path + '/fork', {**body, 'client_session_id': 'd' * 32}, {'If-Match': tag}, 409)
                assert (incomplete / 'marker').read_bytes() == b'preserve incomplete folder'
                evidence.append({'case': 'ordinary_and_incomplete_directories_untouched', 'status': 409})
                concurrent = {**body, 'client_session_id': 'b' * 32}
                def submit(_):
                    status, _, raw = request(port, 'POST', '/api/v1' + path + '/fork',
                        body=json.dumps(concurrent).encode(), headers={'Content-Type': 'application/json',
                            'If-Match': tag, 'X-Mdo-Write-Token': write_token})
                    return status, json.loads(raw)
                with ThreadPoolExecutor(max_workers=2) as pool:
                    pair = list(pool.map(submit, range(2)))
                assert sorted(status for status, _ in pair) == [200, 201], pair
                assert all(document['data']['id'] == 'b' * 32 for _, document in pair)
                evidence.append({'case': 'two_matching_submits_one_child', 'statuses': [status for status, _ in pair]})
                renamed, new_headers = call('PATCH', path, {'title': 'Source changed'}, {'If-Match': tag})
                call('POST', path + '/fork', body, {'If-Match': tag}, 200)
                call('PATCH', path, {'archived': True}, {'If-Match': new_headers['etag']})
                call('POST', path + '/fork', body, {'If-Match': tag}, 200)
                call('POST', path + '/fork', {**body, 'client_session_id': 'f' * 32}, {'If-Match': tag}, 409)
                evidence.append({'case': 'replay_reads_changed_or_archived_source_child_without_new_fork', 'status': 200})
                stop(process)
                process = start(native, packed, home, env)
                assert wait_bootstrap(process, port, native / 'packed.log')[1]['data']['ready']
                call('POST', path + '/fork', body, {'If-Match': tag, 'X-Mdo-Write-Token': write_token}, 412)
                restarted, _ = call('POST', path + '/fork', body, {'If-Match': tag}, 200)
                assert restarted['id'] == first['id'] and target_hash == hashlib.sha256(target.read_bytes()).hexdigest()
                evidence.append({'case': 'restart_replay_and_old_write_fence', 'statuses': [412, 200]})
                listing, _ = call('GET', '/sessions')
                children = [item['id'] for item in listing['items'] if item['parent_session_id'] == source['id']]
                assert sorted(children) == ['a' * 32, 'b' * 32], children
            assert before == hashlib.sha256(snapshot.read_bytes()).hexdigest()
            assert Model.calls == 1
            return {'baseline': baseline, 'cases': evidence, 'source_snapshot_unchanged': True,
                'model_calls': Model.calls, 'passed': True}
        finally:
            stop(process); model.shutdown(); model.server_close(); worker.join(timeout=3)
            release_packed_copies(native / packed.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = check(args.packed.resolve(), args.baseline)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"PASS {'baseline' if args.baseline else 'fixed'} packed fork: {len(result['cases'])} cases")


if __name__ == '__main__':
    main()
