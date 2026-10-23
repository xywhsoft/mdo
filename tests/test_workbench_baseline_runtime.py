"""Session creation captures real raw worktree state in an independent Git DB."""
import argparse, hashlib, http.client, json, os, subprocess, tempfile, time, threading, uuid
from pathlib import Path
from test_api_runtime import ROOT, write_site, free_port, wait_ready
from test_application_sdk_runtime import COMMAND


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT / '.build' if os.name=='nt' else None, prefix='workbench-baseline-') as raw:
        base = Path(raw); home = base / 'home'; repo = home / 'workspace'
        repo.mkdir(parents=True); port = free_port(); config = write_site(base, port)
        commands=home/'commands';commands.mkdir()
        command=COMMAND.replace('mdo_result Code=mdoCall(S,Method,V->At(A,1),R,&E);','mdo_result Code=mdoChangesBaseline(S,V->At(A,1),R,&E);')
        (commands/'baseline.c').write_text('/* mdo-command {"id":"baseline","description":"Baseline C SDK QA"} */\n#include <string.h>\n#include <mdo/workbench.h>\n'+command,encoding='utf-8')
        baseline_source=base/'src/application/changes_baseline.inc.c'
        source=baseline_source.read_text(encoding='utf-8');hook='    /* Validate the complete inventory again, including tracked deletions and'
        assert source.count(hook)==1
        source=source.replace(hook,'    bool Delay=false;xfileinfo DelayInfo={0};if(MdoHomeExternalStat("data/baseline-qa-delay",&Delay,&DelayInfo)&&Delay){(void)MdoHomeAtomicWrite("data/baseline-qa-ready","ready",5,false);xrtSleep(1000);}\n'+hook)
        baseline_source.write_text(source,encoding='utf-8')
        def git(*argv, cwd=repo):
            return subprocess.check_output(['git', '-c', 'user.name=QA', '-c', 'user.email=qa@example.invalid', *argv], cwd=cwd, stderr=subprocess.PIPE)
        git('init', '-b', 'master')
        (repo / 'tracked.txt').write_text('HEAD version\n', encoding='utf-8')
        (repo / 'stable.txt').write_text('original stable\n', encoding='utf-8')
        (repo / 'windows.txt').write_bytes(b'CRLF\r\nunchanged\r\n')
        (repo / 'binary.dat').write_bytes(b'old\0binary')
        (repo / '.gitignore').write_text('ignored.secret\n', encoding='utf-8')
        (repo / '.gitattributes').write_text('tracked.txt filter=tripwire\n', encoding='utf-8')
        (repo / 'large.txt').write_text(''.join(f'before {at}\n' for at in range(1800)),encoding='utf-8')
        git('add', '--all'); git('commit', '-m', 'before session')
        (repo / 'tracked.txt').write_text('staged before session\n',encoding='utf-8');git('add','tracked.txt')
        (repo / 'tracked.txt').write_text('preexisting dirty\n', encoding='utf-8')
        (repo / 'before-untracked.txt').write_text('preexisting untracked\n', encoding='utf-8')
        (repo / 'ignored.secret').write_text('must not snapshot this ignored fixture', encoding='utf-8')
        git('config','filter.tripwire.clean','echo FILTER-RAN > "'+(base/'filter-ran').as_posix()+'"');git('config','filter.tripwire.required','true')
        linked=False
        try:os.symlink(str(base/'outside.txt'),repo/'link.txt');linked=True
        except OSError:pass
        (base/'outside.txt').write_text('outside content must not be followed',encoding='utf-8')
        nested=repo/'nested';nested.mkdir();git('init','-b','master',cwd=nested)
        (nested/'child.txt').write_text('nested start\n');git('add','--all',cwd=nested);git('commit','-m','nested start',cwd=nested)
        index = hashlib.sha256((repo / '.git/index').read_bytes()).hexdigest()
        head = git('rev-parse', 'HEAD'); objects = sorted(str(p.relative_to(repo / '.git/objects')) for p in (repo / '.git/objects').rglob('*') if p.is_file())
        def request(method, path, body=None, expected=200, extra_headers=None):
            headers = {'Content-Type': 'application/json', **(extra_headers or {})}
            if method != 'GET':
                connection = http.client.HTTPConnection('127.0.0.1', port, timeout=35)
                connection.request('GET', '/api/v1/bootstrap'); response = connection.getresponse(); response.read(); headers['X-Mdo-Write-Token'] = response.getheader('X-Mdo-Write-Token'); connection.close()
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=35)
            try:
                connection.request(method, path, json.dumps(body).encode() if body is not None else None, headers)
                response = connection.getresponse(); value = json.loads(response.read()); assert response.status == expected, (response.status, value)
                return value.get('data') if expected < 400 else value['error']
            finally: connection.close()
        def call(method, params=None, session='', expected=200):
            return request('POST', '/api/v1/application/call', {'method': method, 'params': {'repository': str(repo), **(params or {})}, 'context': {'extension': 'qa.baseline', 'project': 'default', 'session': session}}, expected)
        def create():
            return request('POST', '/api/v1/sessions', {'project_id': 'default', 'title': 'Baseline fixture'}, 201)['id']
        def native(session):
            identity='cmd-'+uuid.uuid4().hex
            request('POST','/api/v1/command-invocations?project=default',{'invocation_id':identity,'command_id':'baseline','session_id':session,'raw_arguments':json.dumps(['changes.baseline',{}])},202)
            end=time.monotonic()+20
            while time.monotonic()<end:
                row=request('GET','/api/v1/command-invocations/'+identity)
                if row['terminal']:
                    assert row['state']=='succeeded',row
                    return row['result']
                time.sleep(.05)
            raise AssertionError('C baseline invocation timed out')
        with (base / 'host.log').open('wb') as log:
            process = subprocess.Popen([str(args.host.resolve()), str(config), '--', '--home', str(home)], cwd=base,
                env={**os.environ, 'USE_WEBVIEW': '0', 'GIT_CEILING_DIRECTORIES': str(base)}, stdout=log, stderr=log)
            try:
                wait_ready(port, process)
                session = create(); receipt = call('changes.baseline', session=session)
                assert receipt['available'], receipt
                assert native(session)==receipt,'C and HTTP did not share the original baseline receipt'
                assert not (base/'filter-ran').exists(),'capture executed a configured clean filter'
                initial = call('changes.list', {'scope': 'session'}, session)
                assert initial['files'] == [], initial
                (nested/'child.txt').write_text('nested next\n');git('add','--all',cwd=nested);git('commit','-m','nested next',cwd=nested)
                (repo / 'tracked.txt').write_text('after session\n', encoding='utf-8')
                (repo / 'before-untracked.txt').rename(repo / 'renamed-before.txt')
                (repo / 'new-小说.txt').write_text('new after session\n', encoding='utf-8')
                (repo / 'binary.dat').write_bytes(b'new\0binary')
                state = call('changes.list', {'scope': 'session'}, session); files = {item['path']: item for item in state['files']}
                assert {'tracked.txt', 'renamed-before.txt', 'new-小说.txt', 'binary.dat'} <= set(files), state
                assert 'windows.txt' not in files and 'stable.txt' not in files and 'ignored.secret' not in files
                assert files['renamed-before.txt']['old_path'] == 'before-untracked.txt' and files['binary.dat']['binary']
                assert files['nested']['directory'],state
                nested_diff=call('changes.diff',{'scope':'session','path':'nested'},session)
                assert nested_diff['rows'][0]['kind']=='notice',nested_diff
                diff = call('changes.diff', {'scope': 'session', 'path': 'tracked.txt', 'revision': state['revision']}, session)
                assert any(row.get('text', '').rstrip('\r') == '-preexisting dirty' for row in diff['rows']), diff
                assert any(row.get('text', '').rstrip('\r') == '+after session' for row in diff['rows']), diff
                assert not any(row.get('text', '').rstrip('\r') == '-HEAD version' for row in diff['rows'])
                (repo/'large.txt').write_text(''.join(f'after {at}\n' for at in range(1800)),encoding='utf-8')
                page=call('changes.diff',{'scope':'session','path':'large.txt','offset':3590,'limit':20},session)
                assert page['total']>3600 and any(row.get('text','').rstrip('\r')=='+after 1799' for row in page['rows']),page
                if linked:
                    (base/'outside.txt').write_text('external link target was edited',encoding='utf-8')
                    assert 'link.txt' not in {item['path'] for item in call('changes.list',{'scope':'session'},session)['files']},'snapshot followed a symlink outside the workspace'
                if os.name!='nt':
                    os.chmod(repo/'stable.txt',0o755)
                    assert 'stable.txt' in {item['path'] for item in call('changes.list',{'scope':'session'},session)['files']}
                (repo / 'stable.txt').write_text('changed later\n', encoding='utf-8')
                assert call('changes.diff', {'scope': 'session', 'path': 'tracked.txt', 'revision': state['revision']}, session, 409)['code'] == 'git_comparison_changed'
                assert hashlib.sha256((repo / '.git/index').read_bytes()).hexdigest() == index
                assert git('rev-parse', 'HEAD') == head
                assert sorted(str(p.relative_to(repo / '.git/objects')) for p in (repo / '.git/objects').rglob('*') if p.is_file()) == objects
                # The capture receipt is immutable, including after disk edits.
                assert call('changes.baseline', session=session) == receipt
                info=request('GET',f'/api/v1/projects/default/sessions/{session}')
                fork=request('POST',f'/api/v1/projects/default/sessions/{session}/fork',{'title':'Forked baseline'},201,{'If-Match':f'"mdo-session-{session}-{info["revision"]}"'})['id']
                assert call('changes.baseline',session=fork)['tree']!=receipt['tree'],'Fork reused its parent baseline'
                assert call('changes.list',{'scope':'session'},fork)['files']==[],'Fork did not capture its own start state'
                (repo / 'too-large.bin').write_bytes(b'x' * (16 * 1024 * 1024 + 1))
                limited = create(); failed = call('changes.baseline', session=limited)
                assert not failed['available'] and failed['reason'] == 'git_snapshot_limit', failed
                assert call('changes.list', {'scope': 'session'}, limited, 409)['code'] == 'git_baseline_unavailable'
                (repo / 'too-large.bin').unlink()
                assert call('changes.baseline', session=limited) == failed, 'A failed capture was silently rebased later'
                # A real external edit during a paused capture invalidates the
                # new baseline. The seam exists only in this copied QA app.
                (home/'data').mkdir(exist_ok=True);(home/'data/baseline-qa-delay').write_text('delay');pending=[];failures=[]
                def creating():
                    try:pending.append(create())
                    except Exception as error:failures.append(error)
                worker=threading.Thread(target=creating);worker.start()
                deadline=time.monotonic()+15
                while not (home/'data/baseline-qa-ready').exists() and time.monotonic()<deadline:time.sleep(.02)
                assert (home/'data/baseline-qa-ready').exists()
                (repo/'stable.txt').write_text('external concurrent edit\n',encoding='utf-8');worker.join(timeout=20)
                assert not worker.is_alive() and not failures,failures
                (home/'data/baseline-qa-delay').unlink()
                concurrent=call('changes.baseline',session=pending[0]);assert not concurrent['available'] and concurrent['reason']=='git_snapshot_changed',concurrent
                (home/'data/baseline-qa-ready').unlink();(home/'data/baseline-qa-delay').write_text('delay');pending.clear();worker=threading.Thread(target=creating);worker.start();deadline=time.monotonic()+15
                while not (home/'data/baseline-qa-ready').exists() and time.monotonic()<deadline:time.sleep(.02)
                assert (home/'data/baseline-qa-ready').exists()
                git('commit','--allow-empty','-m','concurrent nested HEAD',cwd=nested);worker.join(timeout=20)
                assert not worker.is_alive() and not failures,failures
                (home/'data/baseline-qa-delay').unlink()
                concurrent=call('changes.baseline',session=pending[0]);assert not concurrent['available'] and concurrent['reason']=='git_snapshot_changed',concurrent
                process.terminate();process.wait(timeout=10)
                process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,env={**os.environ,'USE_WEBVIEW':'0','GIT_CEILING_DIRECTORIES':str(base)},stdout=log,stderr=log);wait_ready(port,process)
                assert call('changes.baseline',session=session)==receipt,'host restart replaced the original baseline'
                # A legacy/imported session has no original capture; querying
                # it must not create a new baseline at the current disk state.
                record = home / 'workbench-baselines/default/sessions' / (session + '.json')
                record.unlink()
                assert call('changes.list', {'scope': 'session'}, session, 409)['code'] == 'git_baseline_missing'
                assert not record.exists()
                if args.evidence:
                    args.evidence.mkdir(parents=True, exist_ok=True)
                    (args.evidence / 'result.json').write_text(json.dumps({'passed': True, 'session': session, 'checks': ['real create baseline', 'C SDK shares receipt', 'fork captures its own start state', 'preexisting staged dirty and untracked excluded', 'raw CRLF preserved', 'rename binary unicode', 'paged source identities', 'clean filters never executed', 'symlink not followed' if linked else 'symlink unsupported by fixture OS', 'stale comparison rejected', 'real index HEAD objects unchanged', 'capture limit does not fail session creation', 'concurrent external edit rejects capture', 'original baseline retained across host restart', 'no retrospective rebase']}, indent=2))
                print('Real session baseline, dirty/untracked state, immutable receipts, private Git objects and limits: PASS')
            except Exception:
                log.flush()
                if args.evidence:
                    args.evidence.mkdir(parents=True, exist_ok=True)
                    (args.evidence / 'host.log').write_bytes((base / 'host.log').read_bytes())
                raise
            finally: process.terminate(); process.wait(timeout=10); time.sleep(1)


if __name__ == '__main__': main()
