"""Bounded live-store acceptance in a temporary Home, using the current app.

Explicit --download is required: one pinned 610 MiB model, two small runtimes.
No app/SDK copies, provider credentials or production writes are used.
"""
import argparse,http.client,json,os,subprocess,tempfile,time
from pathlib import Path
from current_product_host import APP,product_http_config
from test_api_runtime import ROOT,free_port,request,wait_ready
from test_interrupt_runtime import stop_host
VERSION='89862fdae02a5e0fbee835c7526a6b73'
MODEL='local-model:mdo.qwen3-0.6b-q8'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--download',action='store_true');p.add_argument('--packed',type=Path);p.add_argument('--reuse-weight',type=Path,help='Hard-link the verified cached weight into the temporary store');a=p.parse_args()
    if not a.download:p.error('Use --download to authorize the bounded live installation')
    report=dict(passed=False,packed=bool(a.packed),source='production-public-store',version_id=VERSION,checks=[]);process=None
    def check(v,description):assert v,description;report['checks'].append(description);print('PASS',description,flush=True)
    with tempfile.TemporaryDirectory(prefix='local-model-published-',dir=ROOT/'.build') as raw,product_http_config(free_port()) as config:
        base=Path(raw);home=base/'home';home.mkdir();log=base/'runtime.log';port=json.loads(config.read_text(encoding='utf-8'))['services'][0]['port']
        def call(body=None,path='/local-models',timeout=300):
            if body is None:status,_,b=request(port,'GET','/api/v1'+path)
            else:
                _,headers,_=request(port,'GET','/api/v1/bootstrap');token=next(v for k,v in headers.items() if k.lower()=='x-mdo-write-token')
                c=http.client.HTTPConnection('127.0.0.1',port,timeout=timeout)
                try:c.request('POST','/api/v1'+path,json.dumps(body).encode(),{'Content-Type':'application/json','X-Mdo-Write-Token':token});r=c.getresponse();status,b=r.status,r.read()
                finally:c.close()
            v=json.loads(b);assert status==200,(status,v);return v['data']
        def wait(section,id,field,states,seconds=600):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                row=call()[section][id]
                if row[field] in states:return row
                time.sleep(.25)
            raise AssertionError(row)
        try:
            command=[str(ROOT/'.build/host/xs.exe'),str(config),'--','--home',str(home)];cwd=APP
            if a.packed:
                launcher=base/a.packed.name;os.link(a.packed.resolve(),launcher);(base/'xs.json').write_bytes(config.read_bytes());command=[str(launcher),'--','--home',str(home)];cwd=base
            with log.open('wb') as out:process=subprocess.Popen(command,cwd=cwd,env={**os.environ,'MDO_HOME':str(home),'USERPROFILE':str(base),'USE_WEBVIEW':'0'},stdout=out,stderr=out,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process);catalog=call(dict(action='catalog',limit=100));row=next(r for r in catalog['items'] if r['id']==MODEL)
            check(row['version_id']==VERSION and row.get('runtime_available'),'published recipe and both runtime builds are publicly resolvable')
            if a.reuse_weight:
                store=base/'weights';call(dict(action='storage',path=str(store)));os.link(a.reuse_weight.resolve(),store/'blobs/9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031')
            installs=[]
            for backend in ('cpu','vulkan'):
                plan=call(dict(action='plan',version=VERSION,profile=backend+'-16k'));check(plan['runtime_plan']['ready'],backend+' native dependency plan ready')
                job=call(dict(action='install',plan_id=plan['id'],revision=plan['revision'],trust_code=True));job=wait('installations',job['id'],'state',('installed','failed'))
                for attempt in range(2):
                    if job['state']=='installed' or 'connection failed' not in job.get('error',''):break
                    report.setdefault('explicit_retries',[]).append(dict(backend=backend,attempt=attempt+1,error=job['error']));call(dict(action='retry',id=job['id']));job=wait('installations',job['id'],'state',('installed','failed'))
                check(job['state']=='installed',backend+' runtime and fixed weight installed from public endpoints: '+job.get('error',''));installs.append(job['id'])
                d=call(dict(action='deployment_create',installation=job['id'],name='Published '+backend));check(d['config']['reasoning_mode']=='off','starter thinking toggle defaults to off')
                call(dict(action='start',id=d['id'],allow_unknown=backend!='cpu'));state=wait('deployments',d['id'],'service_state',('ready','failed'),120);check(state['service_state']=='ready',backend+' published runtime loaded: '+state.get('service_error',''))
                verification=call(dict(action='verify',id=d['id']));check(verification['verified'] and verification['tools'],backend+' published model streaming and tool round trip passed')
                call(dict(action='stop',id=d['id']));wait('deployments',d['id'],'service_state',('stopped',),30);call(dict(action='deployment_delete',id=d['id'],deployment_revision=d['revision']))
            resource=(base/'weights' if a.reuse_weight else home/'data/local-models/resources')/'blobs';blob_files=list(resource.iterdir());check(len(blob_files)==1 and blob_files[0].stat().st_size==639446688,'CPU and Vulkan installations share one verified 610 MiB weight')
            for id in installs:call(dict(action='uninstall',id=id,delete_files=True))
            check(not call()['installations'] and not list(resource.iterdir()),'published test installations and owned weights removed')
            report['passed']=True
        except BaseException as e:
            report['error']=str(e);raise AssertionError(str(e)+'\n'+log.read_text(encoding='utf-8',errors='replace')[-4000:]) from e
        finally:
            output=ROOT/('.build/local-model-real/acceptance-packed.json' if a.packed else '.build/local-model-real/acceptance-published.json');output.parent.mkdir(exist_ok=True);output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            if process:stop_host(process)
    print('PASS public model store end-to-end acceptance',flush=True)
if __name__=='__main__':main()
