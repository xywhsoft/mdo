"""Exercise selection persistence without model calls or user conversations."""
import argparse
import json
from pathlib import Path
import shutil
import time
import urllib.request
import uuid

def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);a=p.parse_args()
    with urllib.request.urlopen(a.url+'/api/v1/bootstrap') as r: token=r.headers['X-Mdo-Write-Token']
    def call(method,path,body=None,headers=None):
        request=urllib.request.Request(a.url+'/api/v1'+path,method=method,
            data=json.dumps(body).encode() if body is not None else None,
            headers={'Content-Type':'application/json','X-Mdo-Write-Token':token,**(headers or {})})
        with urllib.request.urlopen(request,timeout=10) as r:return json.load(r)['data']
    ids=[uuid.uuid4().hex for _ in range(2)];selection=a.home/'data/workspace-state.json'
    before=selection.read_bytes() if selection.exists() else None
    now=time.time_ns()//1000-10000000
    paths=[]
    try:
        for i,ident in enumerate(ids):
            meta=dict(schema_version=3,revision=1,id=ident,project_id='default',title='Activation QA',agent_id='mdo.default',
                model_id='fixture',protocol='openai-responses',reasoning_effort='',permission_profile='full-access',max_output_tokens=1024,
                workspace_root=str(a.home/'workspace'),created_at_us=now,updated_at_us=now+i,last_activated_at_us=now+i,
                status='active',previous_status='active',pinned=False,config_revision=0,model_generation=0,module_generation=0,skill_generation=0,
                parent_session_id='',forked_through_sequence=0)
            directory=a.home/'sessions/default'/ident;assert not directory.exists();directory.mkdir(parents=True);paths.append(directory)
            (directory/'meta.json').write_text(json.dumps(meta),encoding='utf-8')
        def detail(i):return call('GET','/projects/default/sessions/'+ids[i])
        def select(i):return call('PUT','/workspace-state',dict(project_id='default',session_id=ids[i]))
        unchanged=detail(0);assert unchanged['last_activated_at']==now
        chosen=select(0);stamp=chosen['last_activated_at'];assert stamp>now
        current=detail(0);assert current['revision']==1 and current['updated_at']==now
        assert select(0)['last_activated_at']==stamp, 'repeated acknowledgement must be idempotent'
        assert call('GET','/workspace-state')['last_activated_at']==stamp
        time.sleep(.002);select(1)
        call('PATCH','/projects/default/sessions/'+ids[0],{'title':'Edited without activation'},
             {'If-Match':f'"mdo-session-{ids[0]}-1"'})
        rows=call('GET','/sessions')['items'];owned=[r['id'] for r in rows if r['id'] in ids]
        assert owned==[ids[1],ids[0]],owned
        assert detail(0)['last_activated_at']==stamp
        print('PASS selection ordering, immediate acknowledgement, duplicate PUT, read-only GET and concurrent metadata revision')
    finally:
        if before is not None:selection.write_bytes(before)
        elif selection.exists():selection.unlink()
        for directory in paths:shutil.rmtree(directory)

if __name__=='__main__':main()
