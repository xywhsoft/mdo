"""Packed settings validation, old-Home upgrade and restart persistence."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_api_runtime import request
from test_conversation_groups_runtime import prepare_history_home

ROOT=Path(__file__).resolve().parents[1]


def call(port,method,path,value=None,headers=None):
    body=json.dumps(value).encode() if value is not None else None
    status,metadata,raw=request(port,method,path,body=body,
        headers={**({'Content-Type':'application/json'} if body else {}),**(headers or {})})
    return status,metadata,json.loads(raw)


def save(port,value):
    _,headers,_=call(port,'GET','/api/v1/settings')
    return call(port,'PATCH','/api/v1/settings/settings',
        {'schema_version':1,'patch':{'agent':{'max_turns':value}}},{'If-Match':headers['etag']})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed',type=Path,required=True)
    parser.add_argument('--baseline-packed',type=Path)
    parser.add_argument('--pause-for-ui',action='store_true')
    args=parser.parse_args();packed=args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix='agent-turn-settings-',dir=ROOT/'.build') as raw:
        base=Path(raw);assert base.resolve().is_relative_to((ROOT/'.build').resolve())
        path,port=site(base,'site',args.baseline_packed.resolve() if args.baseline_packed else packed)
        home=base/'home';prepare_history_home(home)
        (home/'config/settings.json').write_text(json.dumps({'schema_version':1,'patch':{'agent':{'memory':False}}}),encoding='utf-8')
        env={**os.environ,'USERPROFILE':str(base),'MDO_ORNITH_RESPONSES_URL':'http://127.0.0.1:1/v1'}
        process=None
        try:
            if args.baseline_packed:
                process=start(path,args.baseline_packed.resolve(),home,env)
                _,boot=wait_bootstrap(process,port,path/'packed.log');assert boot['data']['ready'],boot
                assert '128u' in (home/'agents/default/agent.c').read_text(encoding='utf-8')
                stop(process);process=None
                shutil.copy2(packed,path/packed.name)
            process=start(path,packed,home,env)
            _,boot=wait_bootstrap(process,port,path/'packed.log');assert boot['data']['ready'],boot
            _,_,doc=call(port,'GET','/api/v1/settings');assert doc['data']['agent']['max_turns']==0,doc
            assert 'No role-specific turn ceiling' in (home/'agents/default/agent.c').read_text(encoding='utf-8')
            assert '"max_turns":0' in (home/'subagents/general/agent.md').read_text(encoding='utf-8')
            for value in [0,1024,512]:
                status,_,doc=save(port,value);assert status==200,(status,doc)
                _,_,doc=call(port,'GET','/api/v1/settings');assert doc['data']['agent']['max_turns']==value,doc
            for value in [-1,1.5,1025,True,'5',None]:
                status,_,doc=save(port,value);assert status in (400,422),(value,status,doc)
                _,_,doc=call(port,'GET','/api/v1/settings');assert doc['data']['agent']['max_turns']==512
            stop(process);process=None
            process=start(path,packed,home,env);wait_bootstrap(process,port,path/'packed.log')
            _,_,doc=call(port,'GET','/api/v1/settings');assert doc['data']['agent']['max_turns']==512,doc
            stored=json.loads((home/'config/settings.json').read_text(encoding='utf-8'))
            assert stored['patch']['agent']['max_turns']==512
            assert stored['patch']['agent']['memory'] is False
            status,_,doc=save(port,0);assert status==200,doc
            print('PASS zero default, builtin upgrade, valid/invalid settings, restart persistence and existing user preferences',flush=True)
            if args.pause_for_ui:
                print(f'UI_READY http://127.0.0.1:{port}/#/settings/agent',flush=True)
                input('Press Enter to stop the owned UI fixture.\n')
        finally:
            if process:stop(process)


if __name__=='__main__':main()
