"""Validate real starter archives with native C, then test operator publication."""
import argparse, copy, hashlib, json, os, sqlite3, subprocess, sys, tempfile, time, zipfile
from contextlib import closing
from pathlib import Path
from platform_directory_schema import schema
from test_api_runtime import ROOT,free_port
from test_local_model_format import sign
sys.path.insert(0,str(ROOT/'tools/runtime/build'))
from publish_local_models import publish

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--incoming',type=Path,default=ROOT/'.build/local-model-publication');a=p.parse_args();incoming=a.incoming.resolve()
    rows=json.loads((incoming/'catalog.json').read_text(encoding='utf-8'))['packages'];meta={};docs={}
    for r in rows:
        with zipfile.ZipFile(incoming/r['file']) as z:
            meta[r['version_id']]={**json.loads(z.read('extension.json')),'xid':r['work'],'version_id':r['version_id'],'sha256':r['sha256'],'bytes':r['size']}
            if r['kind']=='local-models':docs[r['version_id']]=json.loads(z.read('model.json'))
    with tempfile.TemporaryDirectory(prefix='local-model-packages-',dir=ROOT/'.build') as raw:
        base=Path(raw);calls=[]
        for r in rows:calls.append('CheckZip('+json.dumps(str(incoming/r['file']))+');')
        for version,doc in docs.items():
            for profile in doc['profiles']:
                runtime=next(r for r in doc['runtimes'] if r['id']==profile['runtime']);files=next(w['files'] for w in doc['weights'] if w['id']==profile['weight'])
                plan=sign(dict(schema='mdo.local-model-plan.v1',platform='windows-x86_64',recipe=meta[version],document=doc,profile=profile,runtime_plan=dict(dependency_protocol=1,root=runtime['ref'],items=[meta[runtime['version_id']]]),assets=files,asset_bytes=sum(f['bytes'] for f in files)))
                path=base/(profile['id']+'.json');path.write_text(json.dumps(plan,ensure_ascii=False,separators=(',',':')),encoding='utf-8');calls.append('CheckPlan('+json.dumps(str(path))+');')
        source=ROOT/'app'/('.qa-'+base.name+'.c');config=source.with_suffix('.json');log=base/'native.log';process=None
        source.write_text('''#include "include/mdo/local_model_plan.h"
static unsigned Failed;
static void CheckZip(cstr P){xfile F=xrtOpen(P,XFILE_READ);MdoZip Z={0};xvalue* M=F&&MdoZipOpen(&Z,F)?MdoExtensionZipManifest(&Z,NULL):NULL;if(!M){++Failed;printf("FAIL ZIP %s %s\\n",P,Z.Error);}xrtValueRelease(M);MdoZipClose(&Z);xrtClose(F);}
static void CheckPlan(cstr P){size_t N=0;char* T=xrtFileReadAllLimit(P,1048576,&N);xvalue* V=T?xrtJsonParse(xrtStrViewN(T,N)):NULL;char E[512]={0};if(!MdoLocalModelPlanValid(V,E)){++Failed;printf("FAIL PLAN %s %s\\n",P,E);}xrtValueRelease(V);xrtFree(T);}
void ServiceInit(XS_HostInfo* H){(void)H;
'''+ '\n'.join(calls)+'\nprintf("native_packages_failed=%u\\n",Failed);fflush(stdout);}\n',encoding='utf-8')
        config.write_text(json.dumps(dict(engine=dict(workers=1),services=[dict(enabled=True,name='packages',**{'class':'http'},ip='127.0.0.1',port=free_port(),host_default=dict(enabled=True,path='web',devlang='c',devfile=source.name))])),encoding='utf-8')
        try:
            with log.open('wb') as out:process=subprocess.Popen([str(ROOT/'.build/host/xs.exe'),str(config)],cwd=ROOT/'app',env={**os.environ,'USE_WEBVIEW':'0'},stdout=out,stderr=out,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            end=time.monotonic()+30
            while process.poll() is None and time.monotonic()<end:
                if 'native_packages_failed=' in log.read_text(encoding='utf-8',errors='replace'):break
                time.sleep(.1)
            text=log.read_text(encoding='utf-8',errors='replace');assert 'native_packages_failed=0' in text,text
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=8)
            source.unlink(missing_ok=True);config.unlink(missing_ok=True)
        headers=ROOT/'app/include/mdo';proof=dict(passed=True,validator_sha256=hashlib.sha256(b''.join((headers/n).read_bytes() for n in ('local_model_plan.h','local_model_format.h','extension_manifest.h'))).hexdigest(),packages=rows)
        (incoming/'native-validation.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
        dbpath=base/'catalog.db'
        with closing(sqlite3.connect(dbpath)) as db:db.executescript(schema());db.commit()
        preview=publish(incoming,dbpath,base/'preview.db');assert len(preview)==3 and not (base/'preview.db').exists()
        result=publish(incoming,dbpath,base/'backup.db',True);assert result['passed']
        publish(incoming,dbpath,base/'repeat.db',True)
        with closing(sqlite3.connect(dbpath)) as db:
            assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
            assert db.execute('SELECT count(*) FROM version').fetchone()[0]==3
            assert db.execute('SELECT count(*) FROM local_model_index').fetchone()[0]==1
            assert db.execute('SELECT count(*) FROM identity').fetchone()[0]==3
            assert db.execute("SELECT count(*) FROM upload WHERE state='sealed'").fetchone()[0]==0
            db.execute("UPDATE work SET withdrawn=1 WHERE kind='local-models'");db.commit()
        try:publish(incoming,dbpath,base/'withdrawn.db',True)
        except ValueError:pass
        else:raise AssertionError('Withdrawn work was silently restored')
        # The live receipt comes only from the production invocation.
        (incoming/'publication-receipt.json').unlink(missing_ok=True)
    print('PASS native real runtime/recipe ZIPs and fixed plans; operator transaction, immutable identities, idempotence, index and withdrawn-work protection')
if __name__=='__main__':main()
