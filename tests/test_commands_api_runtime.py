"""Command jobs: real parent/child execution, cancellation, deadline and replay."""
import argparse,json,os,subprocess,tempfile,time,uuid,shutil
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready

def fixture(id,body,timeout=30000):
    return '/* mdo-command '+json.dumps({'id':id,'description':'fixture','timeout_ms':timeout})+' */\n#include "mdo/command.h"\n'+body

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',required=True,type=Path);p.add_argument('--packed',action='store_true');args=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='commands-api-') as tmp:
        base=Path(tmp);port=free_port();home=base/'home';host=args.host.resolve()
        if args.packed:
            shutil.copy2(host,base/'mdo.exe');host=base/'mdo.exe';config=base/'xs.json'
            config.write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'commands-packed','ip':'127.0.0.1','port':port,
              'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}),encoding='utf-8')
        else:config=write_site(base,port)
        (home/'commands').mkdir(parents=True)
        legacy='---\ndescription: Legacy review\n---\nReview $ARGUMENTS carefully.'
        (home/'commands/legacy.md').write_text(legacy,encoding='utf-8')
        commands=home/'commands' if args.packed else base/'default-home/commands';commands.mkdir(parents=True,exist_ok=True)
        bodies={
          'echo':'mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){char* J=C->Host->Json(A);*R=C->Host->Parse(J);C->Host->Free(J);return MDO_RESULT_OK;}',
          'quit':'#include <stdlib.h>\nmdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){exit(17);return MDO_RESULT_ERROR;}',
          'loop':'mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){for(;;){}return MDO_RESULT_ERROR;}',
          'broken':'syntax error',
          'bridge':'mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){return mdoCommandCall(C,"ui.help",A,R);}',
          'api':'mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){xvalue* P=C->Host->Parse("{\\"method\\":\\"GET\\",\\"path\\":\\"/api/v1/bootstrap\\"}");mdo_result E=mdoCommandCall(C,"api.request",P,R);C->Host->Release(P);return E;}',
        }
        for name,body in bodies.items():(commands/f'{name}.c').write_text(fixture(name,body,1000 if name=='loop' else 30000),encoding='utf-8')
        log=base/'host.log'
        with log.open('wb') as output:
            process=subprocess.Popen([str(host),*([] if args.packed else [str(config)]),'--','--home',str(home)],cwd=base,stdout=output,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            try:
                wait_ready(port,process)
                def invoke(name,raw='',identity=None):
                    identity=identity or 'cmd-'+uuid.uuid4().hex
                    payload={'invocation_id':identity,'command_id':name,'raw_arguments':raw,'session_id':'fixture'}
                    status,_,body=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps(payload).encode(),headers={'Content-Type':'application/json'})
                    assert status==202,(status,body,request(port,'GET','/api/v1/commands?project=default')[2],log.read_text(encoding='utf-8',errors='replace'))
                    return identity
                def wait(identity):
                    until=time.monotonic()+15
                    while time.monotonic()<until:
                        status,_,body=request(port,'GET','/api/v1/command-invocations/'+identity)
                        result=json.loads(body)['data'];assert status==200,(status,result)
                        if result['terminal']:return result
                        time.sleep(.03)
                    raise AssertionError(('command deadline',identity,log.read_text(encoding='utf-8',errors='replace')))
                identity=invoke('echo','["",true,3,{"x":[1]}]');result=wait(identity)
                assert result['state']=='succeeded' and result['result']==['',True,3,{'x':[1]}],result
                assert invoke('echo','["",true,3,{"x":[1]}]',identity)==identity
                assert wait(identity)==result,'Replay changed the receipt'
                for name in ('quit','broken'):
                    result=wait(invoke(name));assert result['state']=='failed' and result['error'],result
                result=wait(invoke('loop'));assert result['state']=='timed_out',result
                identity=invoke('loop');request(port,'DELETE','/api/v1/command-invocations/'+identity)
                assert wait(identity)['state']=='cancelled'
                result=wait(invoke('bridge'));assert result['state']=='succeeded' and result['actions'][0]['type']=='ui.help',result
                result=wait(invoke('api'));assert result['state']=='succeeded' and result['result']['status']==200,result
                expected={'new':'ui.new','model':'ui.model','fork':'ui.session','export':'ui.session',
                  'clear':'ui.session','stop':'ui.stop','settings':'ui.settings','theme':'ui.theme','help':'ui.help','mcp':'ui.mcp'}
                for name,action in expected.items():
                    result=wait(invoke(name))
                    assert result['state']=='succeeded' and result['actions'][0]['type']==action,(name,result)
                    if name in ('fork','export','clear'):assert result['actions'][0]['params']['action']==name,result
                result=wait(invoke('new','unexpected'))
                assert result['state']=='failed' and 'Invalid arguments for /new' in result['error'],result
                result=wait(invoke('legacy','"C:\\new folder\\chapter.txt"'))
                assert result['state']=='succeeded' and result['actions'][0]['type']=='skill.invoke',result
                skill=result['actions'][0]['params']['id']
                status,_,raw=request(port,'POST','/api/v1/skills/invoke?project=default',
                    body=json.dumps({'id':skill,'arguments':'hello world'}).encode(),headers={'Content-Type':'application/json'})
                assert status==200 and 'Review hello world carefully.' in json.loads(raw)['data']['text'],raw
                assert (home/'commands/legacy.md').read_text(encoding='utf-8')==legacy
                old=json.loads(request(port,'GET','/api/v1/extensions/commands/legacy')[2])['data']
                status,_,raw=request(port,'DELETE','/api/v1/extensions/commands/legacy',headers={'If-Match':'"'+old['revision']+'"'})
                assert status==200,(status,raw)
                builtin=json.loads(request(port,'GET','/api/v1/extensions/commands/help')[2])['data']['content']
                path='/api/v1/commands/project/help?project=default'
                status,_,raw=request(port,'PUT',path,body=json.dumps({'content':builtin}).encode(),headers={'Content-Type':'application/json','If-Match':'"new"'})
                assert status==200,(status,raw)
                revision=json.loads(raw)['data']['revision']
                assert json.loads(request(port,'GET','/api/v1/commands?project=default')[2])['data']['help']['scope']=='project'
                toggle='/api/v1/commands/project/help/enabled?project=default'
                for enabled in (False,True):
                    status,_,raw=request(port,'POST',toggle,body=json.dumps({'enabled':enabled}).encode(),headers={'Content-Type':'application/json','If-Match':'"'+revision+'"'})
                    assert status==200,(status,raw)
                    assert json.loads(request(port,'GET','/api/v1/commands?project=default')[2])['data']['help']['enabled']==enabled
                status,_,_=request(port,'PUT',path,body=json.dumps({'content':builtin}).encode(),headers={'Content-Type':'application/json','If-Match':'"new"'})
                assert status==412
                status,_,raw=request(port,'DELETE',path,headers={'If-Match':'"'+revision+'"'})
                assert status==200,(status,raw)
                assert json.loads(request(port,'GET','/api/v1/commands?project=default')[2])['data']['help']['scope']=='builtin'
                damaged='cmd-'+uuid.uuid4().hex
                (home/'data/commands'/f'{damaged}.json').write_text('corrupt receipt',encoding='utf-8')
                status,_,raw=request(port,'GET','/api/v1/command-invocations/'+damaged)
                assert status==200 and json.loads(raw)['data']['state']=='uncertain',raw
                status,_,raw=request(port,'POST','/api/v1/command-invocations?project=default',
                    body=json.dumps({'invocation_id':damaged,'command_id':'help'}).encode(),headers={'Content-Type':'application/json'})
                assert status==409,raw
                assert (home/'data/commands'/f'{damaged}.json').read_text(encoding='utf-8')=='corrupt receipt'
                process.terminate();process.wait(timeout=10)
                process=subprocess.Popen([str(host),*([] if args.packed else [str(config)]),'--','--home',str(home)],cwd=base,stdout=output,stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                wait_ready(port,process)
                assert wait(invoke('help'))['state']=='succeeded','One corrupt receipt disabled normal commands after restart'
                assert 'legacy' not in json.loads(request(port,'GET','/api/v1/commands?project=default')[2])['data'],'Deleted migrated command was resurrected from its backup'
                assert (home/'data/commands'/f'{damaged}.json').read_text(encoding='utf-8')=='corrupt receipt'
                assert not list((home/'data/cache/command-workers').glob('*/input.json')),'Sensitive worker inputs retained'
                assert process.poll() is None,'Command terminated the host'
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    print('Command API isolated worker / failure / deadline / cancellation / replay / loopback: PASS')
if __name__=='__main__':main()
