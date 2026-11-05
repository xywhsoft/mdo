"""Real API/SDK project launch routing, with the OS opener captured in C."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host

SOURCE = r'''
#include <xsbase.h>
#include <stdio.h>
#include <string.h>
static bool QaProjectOpen(cstr Path) {
    printf("QA_PROJECT_OPEN:%s\n", Path); fflush(stdout);
    return strstr(Path, "opener-failure") == NULL;
}
#define xrtProcessOpen QaProjectOpen
#include "generated/mdo_unity.c"
'''


def run(host):
    with tempfile.TemporaryDirectory(prefix='project-open-', dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';home=base/'home';shutil.copytree(ROOT/'app',site)
        workspace=home/'workspaces'/'space + 中文 & folder';workspace.mkdir(parents=True)
        failing=base/'opener-failure';failing.mkdir()
        missing=base/'missing';missing.mkdir()
        file_target=base/'file-target';file_target.mkdir()
        (site/'project-open.c').write_text(SOURCE,encoding='utf-8')
        port=free_port()
        (site/'xs.json').write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'qa','ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'project-open.c'}}]}),encoding='utf-8')
        log_path=base/'runtime.log';process=None
        def call(method,path,body=None):
            status,_,raw=request(port,method,path,body=json.dumps(body).encode() if body is not None else None,headers={'Content-Type':'application/json'})
            return status,json.loads(raw) if raw else {}
        def opened():
            return [row.removeprefix('QA_PROJECT_OPEN:') for row in log_path.read_text(encoding='utf-8',errors='replace').splitlines() if row.startswith('QA_PROJECT_OPEN:')]
        def open_project(identity,body=None):
            return call('POST','/api/v1/projects/'+identity+'/open-directory',{} if body is None else body)
        try:
            with log_path.open('wb') as log:
                process=subprocess.Popen([str(host),str(site/'xs.json'),'--','--home',str(home)],cwd=base,stdout=log,stderr=log,env={**os.environ,'USE_WEBVIEW':'0'},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                wait_ready(port,process)
                for identity,path in [('chosen',workspace),('broken',missing),('file',file_target),('failure',failing)]:
                    status,value=call('POST','/api/v1/projects',{'id':identity,'name':identity,'workspace_root':str(path),'default_model_id':''})
                    assert status==201,(status,value)
                status,value=open_project('chosen');assert status==200 and value['data']=={'project_id':'chosen','opened':True},value
                assert Path(opened()[-1]).resolve()==workspace.resolve(),opened()
                status,value=call('POST','/api/v1/application/call',{'method':'project.open_directory','params':{'path':{'project':'chosen'},'body':{}},'context':{'project':'default'}})
                assert status==200 and value['data']['status']==200,value
                assert Path(opened()[-1]).resolve()==workspace.resolve()
                status,value=open_project('default');assert status==200,value
                assert Path(opened()[-1]).resolve()==(home/'workspace').resolve() and (home/'workspace').is_dir()
                before=len(opened())
                for body in ({'path':str(failing)},[],{'command':'must-not-run'}):
                    status,value=open_project('chosen',body);assert status==400,value
                assert open_project('unknown')[0]==404
                missing.rmdir();file_target.rmdir();file_target.write_text('preserve',encoding='utf-8')
                assert open_project('broken')[0]==422
                assert open_project('file')[0]==422
                assert call('GET','/api/v1/projects/chosen/open-directory')[0]==405
                assert call('POST','/api/v1/projects/chosen/open-directory?path=elsewhere',{})[0]==400
                assert len(opened())==before,'Invalid requests must never reach the OS opener'
                status,value=open_project('failure');assert status==503 and value['error']['code']=='desktop_unavailable',value
                assert len(opened())==before+1 and file_target.read_text()=='preserve'
        except Exception:
            print(log_path.read_text(encoding='utf-8',errors='replace')[-4000:]);raise
        finally:stop_host(process)
    print('PASS project workspace opener: registered/default paths, SDK, Unicode, invalid body, missing directory and native failure')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
