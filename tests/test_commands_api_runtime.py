"""Command jobs: real parent/child execution, cancellation, deadline and replay."""
import argparse,json,os,subprocess,tempfile,time,uuid
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready

def fixture(id,body,timeout=30000):
    return '/* mdo-command '+json.dumps({'id':id,'description':'fixture','timeout_ms':timeout})+' */\n#include "mdo/command.h"\n'+body

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',required=True,type=Path);args=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='commands-api-') as tmp:
        base=Path(tmp);port=free_port();config=write_site(base,port);home=base/'home'
        commands=base/'default-home/commands';commands.mkdir(exist_ok=True)
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
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,stdout=output,stderr=subprocess.STDOUT,
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
                assert not list((home/'data/cache/command-workers').glob('*/input.json')),'Sensitive worker inputs retained'
                assert process.poll() is None,'Command terminated the host'
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    print('Command API isolated worker / failure / deadline / cancellation / replay / loopback: PASS')
if __name__=='__main__':main()
