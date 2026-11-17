"""Actual client C validators; synthetic recipes only, no downloads."""
import argparse, copy, hashlib, json, os, subprocess, tempfile, time, zipfile
from pathlib import Path
from test_api_runtime import ROOT, free_port
from local_model_fixture import recipe, manifest

def plan():
    model=recipe(); meta=manifest();meta.update(xid='1'*32,version_id='2'*32,sha256='d'*64,bytes=1000)
    runtime=dict(id=model['runtimes'][0]['ref'],kind='external-tools',xid='3'*32,version_id='1'*32,sha256='c'*64)
    value=dict(schema='mdo.local-model-plan.v1',platform='windows-x86_64',recipe=meta,document=model,profile=copy.deepcopy(model['profiles'][0]),runtime_plan=dict(dependency_protocol=1,root=runtime['id'],items=[runtime]),assets=copy.deepcopy(model['weights'][0]['files']),asset_bytes=5*1024**3)
    return sign(value)

def sign(value):
    value.pop('fingerprint',None);value['fingerprint']=hashlib.sha256(json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()).hexdigest();return value

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='local-model-format-',dir=ROOT/'.build') as raw:
        base=Path(raw);calls=[];cases=[('valid',plan(),True)]
        for name,change in [
            ('profile',lambda p:p['profile'].update(context_tokens=16384)),
            ('assets',lambda p:p['assets'][0].update(bytes=1)),
            ('runtime-version',lambda p:p['runtime_plan']['items'][0].update(version_id='9'*32)),
            ('runtime-hash',lambda p:p['runtime_plan']['items'][0].update(sha256='9'*64)),
            ('runtime-root',lambda p:p['runtime_plan'].update(root='external-tool:other')),
            ('bytes',lambda p:p.update(asset_bytes=1)),
            ('platform',lambda p:p.update(platform='linux-x86_64')),
            ('secret-url',lambda p:p['document']['weights'][0]['files'][0].update(urls=['https://example.test/file?token=secret'])),
            ('budget',lambda p:p['document']['profiles'][0].update(output_tokens=100000)),
        ]:
            value=plan();change(value);cases.append((name,sign(value),False))
        value=plan();value['fingerprint']='0'*64;cases.append(('fingerprint',value,False))
        for name,value,expected in cases:
            path=base/(name+'.json');path.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':')),encoding='utf-8');calls.append(f'CheckPlan({json.dumps(str(path))},{str(expected).lower()});')
        for name,extra,good in [('valid',{},True),('weights',{'model.gguf':'GGUF'},False),('program',{'run.exe':'MZ'},False),('size',{'large.txt':'x'*262145},False)]:
            path=base/(name+'.zip')
            with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
                z.writestr('extension.json',json.dumps(manifest()));z.writestr('model.json',json.dumps(recipe()))
                for key,value in extra.items():z.writestr(key,value)
            calls.append(f'CheckZip({json.dumps(str(path))},{str(good).lower()});')
        app=ROOT/'app';source=app/('.qa-'+base.name+'.c');config=source.with_suffix('.json')
        source.write_text(r'''
#include "include/mdo/local_model_plan.h"
static unsigned Failed,Count;
static void CheckPlan(cstr Path,bool Expected){size_t N=0;char* S=xrtFileReadAllLimit(Path,2u*1024u*1024u,&N);xvalue* P=S?xrtJsonParse(xrtStrViewN(S,N)):NULL;char E[512]={0};bool Actual=MdoLocalModelPlanValid(P,E);++Count;if(Actual!=Expected){++Failed;printf("FAIL %s %s\n",Path,E);}xrtValueRelease(P);xrtFree(S);}
static void CheckZip(cstr Path,bool Expected){xfile F=xrtOpen(Path,XFILE_READ);MdoZip Z={0};bool Open=F&&MdoZipOpen(&Z,F);xvalue* M=Open?MdoExtensionZipManifest(&Z,NULL):NULL;++Count;if((M!=NULL)!=Expected){++Failed;printf("FAIL ZIP %s %s\n",Path,Z.Error);}xrtValueRelease(M);MdoZipClose(&Z);xrtClose(F);}
void ServiceInit(XS_HostInfo* H){
'''+'\n'.join(calls)+'\nprintf("format_checks=%u\\nformat_failed=%u\\nprobe_done=1\\n",Count,Failed);fflush(stdout);}\n',encoding='utf-8')
        config.write_text(json.dumps(dict(engine=dict(workers=1),services=[dict(enabled=True,name='format',**{'class':'http'},ip='127.0.0.1',port=free_port(),host_default=dict(enabled=True,path='web',devlang='c',devfile=source.name))])),encoding='utf-8');process=None
        try:
            log=base/'runtime.log'
            with log.open('wb') as out:process=subprocess.Popen([str(args.host.resolve()),str(config)],cwd=app,env={**os.environ,'USE_WEBVIEW':'0'},stdout=out,stderr=out,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            end=time.monotonic()+50
            while process.poll() is None and time.monotonic()<end:
                if 'probe_done=1' in log.read_text(encoding='utf-8',errors='replace'):break
                time.sleep(.1)
            text=log.read_text(encoding='utf-8',errors='replace');assert 'format_failed=0' in text and 'probe_done=1' in text,text[-4500:];print('\n'.join(x for x in text.splitlines() if x.startswith('format_')))
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            source.unlink(missing_ok=True);config.unlink(missing_ok=True)

if __name__=='__main__':main()
