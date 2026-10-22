"""Execute real isolated TCC commands; no model or user data is touched."""
import argparse,json,os,shutil,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def worker(host,root,source,arguments):
    root.mkdir();(root/'mdo').mkdir();(root/'web').mkdir()
    (root/'web/index.html').write_text('worker',encoding='utf-8')
    for name in ('command.h','module.h'):shutil.copy2(ROOT/'include/mdo'/name,root/'mdo'/name)
    shutil.copy2(ROOT/'app/command-worker.c',root/'worker.c')
    (root/'command.c').write_text('#include "mdo/command.h"\n'+source,encoding='utf-8')
    (root/'input.json').write_text(json.dumps({'invocation_id':'fixture','command_id':'fixture','arguments':arguments}),encoding='utf-8')
    config={'services':[{'enabled':True,'class':'custom','name':'worker',
      'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'worker.c'}}]}
    (root/'xs.json').write_text(json.dumps(config),encoding='utf-8')
    p=subprocess.Popen([str(host),str(root/'xs.json'),'--no-vfs'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    return p

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);a=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='command-worker-') as tmp:
        root=Path(tmp)
        p=worker(a.host.resolve(),root/'echo',r'''
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R) {
    char* J=C->Host->Json(A);*R=C->Host->Parse(J);C->Host->Free(J);return MDO_RESULT_OK;
}''',['',r'C:\path',True,3,{'nested':[1]}])
        try:
            log=p.communicate(timeout=15)[0].decode('utf-8',errors='replace')
            assert (root/'echo/final.json').exists(),log
            result=json.loads((root/'echo/final.json').read_text(encoding='utf-8'))
            assert result['ok'] and result['result']==['',r'C:\path',True,3,{'nested':[1]}],result
        finally:
            if p.poll() is None:p.kill();p.wait()
        p=worker(a.host.resolve(),root/'bridge',r'''
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R) {
    return mdoCommandCall(C,"ui.help",A,R);
}''',[])
        try:
            until=time.monotonic()+12
            request=root/'bridge/request-1.json'
            while not request.exists() and p.poll() is None and time.monotonic()<until:time.sleep(.02)
            assert request.exists(),p.communicate(timeout=2)[0].decode('utf-8',errors='replace')
            assert json.loads(request.read_text(encoding='utf-8'))=={'method':'ui.help','params':[]}
            (root/'bridge/response-1.json').write_text('{"ok":true,"result":{"accepted":true}}',encoding='utf-8')
            log=p.communicate(timeout=10)[0].decode('utf-8',errors='replace')
            result=json.loads((root/'bridge/final.json').read_text(encoding='utf-8'))
            assert result['ok'] and result['result']=={'accepted':True},(result,log)
        finally:
            if p.poll() is None:p.kill();p.wait()
    print('Isolated command typed values / host RPC: PASS')
if __name__=='__main__':main()
