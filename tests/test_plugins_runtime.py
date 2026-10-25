"""Unified plugins through a real isolated XS/TCC host; no provider requests."""
import hashlib
import base64
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

class PluginTemporaryDirectory(tempfile.TemporaryDirectory):
    @classmethod
    def _rmtree(cls,name,*args,**kwargs):
        if os.name=="nt" and not name.startswith("\\\\?\\"):
            name="\\\\?\\"+str(Path(name).resolve())
        return super()._rmtree(name,*args,**kwargs)

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    with PluginTemporaryDirectory(dir=ROOT/'.build',prefix='plugins-runtime-') as tmp:
        base=Path(tmp);port=free_port();config=write_site(base,port)
        with (base/'probe.log').open('wb') as log:
            child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                try: wait_ready(port,child)
                except Exception:
                    log.flush();raise RuntimeError((base/'probe.log').read_text(encoding='utf-8',errors='replace')[-8000:])
                def call(method,params=None,status=200):
                    code,_,raw=request(port,'POST','/api/v1/application/call',body=json.dumps(dict(method=method,params=params or {})).encode(),headers={'Content-Type':'application/json'})
                    value=json.loads(raw);assert code==status,(method,code,value)
                    return value['data'] if status==200 else value['error']
                def install(id,**fields):
                    doc=dict(format='mdo.plugin.v1',id=id,name=id,version='1.0.0',description='Offline fixture',
                        overrides=[dict(source='value.txt',target='web/plugin-fixture/'+id+'.txt')])
                    doc.update(fields)
                    return call('plugin.apply',dict(id=id,manifest=doc,files=[dict(path='value.txt',text=id)]))
                def plan(changes):return call('plugin.plan',dict(changes=changes))
                def commit(changes):
                    p=plan(changes);assert p['allowed'],p
                    return call('plugin.commit',dict(changes=changes,expected_revision=p['base_revision'],expected_plan=p['plan_revision']))
                install('base')
                a=install('alpha',exclusive=['mdo.agent.default-orchestrator'])
                assert all(not r['enabled'] for r in a['items']),a
                install('beta',exclusive=['mdo.agent.default-orchestrator'])
                commit([dict(id='alpha',enabled=True)])
                p=plan([dict(id='beta',enabled=True)])
                assert not p['allowed'] and any(r['code']=='exclusive' for r in p['issues']),p
                assert call('plugin.enable',dict(id='beta'),409)['code']=='sdk_plugin_conflict'
                commit([dict(id='alpha',enabled=False),dict(id='beta',enabled=True)])
                assert call('vfs.read',dict(path='/app/web/plugin-fixture/beta.txt'))['text']=='beta'
                assert call('vfs.read',dict(path='/app/web/plugin-fixture/alpha.txt'),404)
                install('worker',requires=[dict(id='base',min_version='1.0.0',max_version_exclusive='2.0.0')])
                p=plan([dict(id='worker',enabled=True)]);assert not p['allowed'] and p['issues'][0]['code']=='requires',p
                commit([dict(id='base',enabled=True),dict(id='worker',enabled=True)])
                p=plan([dict(id='base',enabled=False)]);assert not p['allowed']
                call('plugin.remove',dict(id='base'),409)
                p=plan([dict(id='beta',enabled=False)])
                install('additional')
                call('plugin.commit',dict(changes=[dict(id='beta',enabled=False)],expected_revision=p['base_revision'],expected_plan=p['plan_revision']),412)
                install('incompatible',conflicts=[dict(id='beta',reason='one-sided conflict')])
                p=plan([dict(id='incompatible',enabled=True)]);assert not p['allowed'] and p['issues'][0]['code']=='conflicts',p
                install('cycle-a',requires=[dict(id='cycle-b')]);install('cycle-b',requires=[dict(id='cycle-a')])
                p=plan([dict(id='cycle-a',enabled=True),dict(id='cycle-b',enabled=True)])
                assert not p['allowed'] and any(r['code']=='dependency_cycle' for r in p['issues']),p
                previous=call('plugin.list')
                doc=dict(format='mdo.plugin.v1',id='base',name='base',version='2.0.0',description='Breaks dependency',overrides=[dict(source='value.txt',target='web/plugin-fixture/base.txt')])
                call('plugin.apply',dict(id='base',manifest=doc,files=[dict(path='value.txt',text='new')]),409)
                assert call('plugin.list')==previous,'A rejected update changed the published plugin set'
                commit([dict(id='worker',enabled=False),dict(id='base',enabled=False)])
                def frontend(id):
                    doc=dict(format='mdo.plugin.v1',id=id,name=id,version='1.0.0',description='Frontend fixture',entry=dict(frontend='runtime/main.js'),exclusive=['mdo.ui.chat-layout'])
                    result=call('plugin.apply',dict(id=id,manifest=doc,files=[dict(path='runtime/main.js',text='export function activate(sdk) {}')]))
                    return next(r for r in result['items'] if r['id']==id)['revision']
                ra=frontend('front-a');rb=frontend('front-b')
                commit([dict(id='front-a',enabled=True)])
                def window(action,client,id,revision):return call('plugin.frontend.'+action,dict(client=client,page='page-one',id=id,revision=revision))
                window('enter','window-a','front-a',ra);window('enter','window-b','front-a',ra)
                result=commit([dict(id='front-a',enabled=False),dict(id='front-b',enabled=True)])
                assert result['pending'] and not result['restart_required'],result
                call('plugin.frontend.enter',dict(client='window-c',page='page-one',id='front-b',revision=rb),409)
                window('leave','window-a','front-a',ra);assert call('plugin.list')['pending']
                window('leave','window-b','front-a',ra);assert not call('plugin.list')['pending']
                window('enter','window-c','front-b',rb)
                call('plugin.frontend.hello',dict(client='window-c',page='new-document'))
                native_source=r'''
#include "mdo/sdk.h"
#include "extra.h"
static unsigned Starts;
static mdo_result Counter(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)P;(void)E;*R=C->Api->Values->UInt(Starts+EXTRA);return MDO_RESULT_OK;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 (void)H;(void)D;Starts++;
 mdo_service_v1 S={sizeof(S),1,"qa.plugin.counter","Plugin counter","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Counter};
 return R->AddService(R->Context,&S,E,N);
}
static const mdo_module_v1 M={MDO_V1_HEADER(mdo_module_v1),"plugin.native-fixture","Plugin fixture","Fixture","1.0.0",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};
const mdo_module_v1* mdoModuleEntry(void){return &M;}
'''
                doc=dict(format='mdo.plugin.v1',id='native-fixture',name='Native',version='1.0.0',description='Native fixture',entry=dict(native='runtime/plugin.c'))
                call('plugin.apply',dict(id='native-fixture',manifest=doc,files=[dict(path='runtime/plugin.c',text=native_source),dict(path='runtime/extra.h',text='#define EXTRA 10\n')]))
                result=commit([dict(id='native-fixture',enabled=True)])
                assert result['pending'] and result['restart_required'],result
                assert call('runtime.reload',dict(domain='host'),409)['code']=='sdk_plugin_restart_required'
                call('qa.plugin.counter',status=404)
                child.terminate();child.wait(timeout=10)
                child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                try: wait_ready(port,child)
                except Exception:
                    log.flush();raise RuntimeError((base/'probe.log').read_text(encoding='utf-8',errors='replace')[-8000:])
                assert call('qa.plugin.counter')==11
                call('runtime.reload',dict(domain='modules'))
                assert call('qa.plugin.counter')==11,'An unchanged native plugin was registered twice'
                presets=call('plugin.preset.list')
                saved=call('plugin.preset.save',dict(id='development',name='Development',expected_revision=presets['revision']))
                assert saved['items'][0]['plugins'] and all(set(p)=={'id','revision','enabled'} for p in saved['items'][0]['plugins'])
                call('plugin.preset.save',dict(id='other',name='Other',expected_revision=presets['revision']),412)
                commit([dict(id='beta',enabled=False)])
                assert not call('plugin.preset.list')['items'][0]['matches']
                p=call('plugin.preset.plan',dict(id='development'));assert p['allowed'],p
                call('plugin.commit',dict(changes=p['changes'],expected_revision=p['base_revision'],expected_plan=p['plan_revision']))
                assert call('plugin.preset.list')['items'][0]['matches']
                info=call('plugin.get',dict(id='native-fixture'))
                f=call('plugin.file.read',dict(id='native-fixture',path='runtime/plugin.c',offset=0,limit=17))
                assert f['next']==17 and f['total']==len(native_source.encode()) and info['files']
                # Upload references use the caller namespace, bounded reads and hashes.
                upload=base/'home/data/extensions/local-client/uploads/large.bin'
                upload.parent.mkdir(parents=True,exist_ok=True);payload=b'bounded upload'*(90*1024);upload.write_bytes(payload)
                deep='docs/.knowledge/'+('/'.join(['folder'*8]*3))+'/index.md'
                upload_doc=dict(format='mdo.plugin.v1',id='uploaded',name='Uploaded',version='1.0.0',description='Upload fixture',overrides=[dict(source=deep,target='web/deep/index.md')])
                files=[dict(path=deep,storage_path='uploads/large.bin',bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())]
                installed=call('plugin.apply',dict(id='uploaded',manifest=upload_doc,files=files));rev=next(r['revision'] for r in installed['items'] if r['id']=='uploaded')
                reused=call('plugin.apply',dict(id='uploaded',manifest=upload_doc,files=[dict(path=deep,plugin_revision=rev,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())]));assert next(r['revision'] for r in reused['items'] if r['id']=='uploaded')==rev
                other=upload.with_name('same.bin');other.write_bytes(payload);files[0]['storage_path']='uploads/same.bin'
                installed=call('plugin.apply',dict(id='uploaded',manifest=upload_doc,files=files));assert next(r['revision'] for r in installed['items'] if r['id']=='uploaded')==rev
                next_doc=dict(upload_doc,version='2.0.0')
                call('plugin.apply',dict(id='uploaded',manifest=next_doc,files=[dict(path=deep,plugin_revision=rev,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())]))
                restored=call('plugin.rollback',dict(id='uploaded'));assert next(r for r in restored['items'] if r['id']=='uploaded')['manifest']['version']=='1.0.0'
                files[0]['sha256']='0'*64;call('plugin.apply',dict(id='uploaded',manifest=upload_doc,files=files),409)
                files[0]['storage_path']='../escape';call('plugin.apply',dict(id='uploaded',manifest=upload_doc,files=files),422)
                # Broken native code must leave a usable recovery UI and preserve
                # the durable desired selection. Repair never replays that code.
                for mode in ['syntax','register','duplicate']:
                    badid='broken-'+mode
                    source='this is not C;' if mode=='syntax' else native_source.replace('Starts++;','Starts++; return MDO_RESULT_ERROR;' if mode=='register' else 'Starts++;').replace('plugin.native-fixture','plugin.'+badid)
                    doc=dict(format='mdo.plugin.v1',id=badid,name='Broken',version='1.0.0',description='Recovery fixture',entry=dict(native='runtime/plugin.c'))
                    call('plugin.apply',dict(id=badid,manifest=doc,files=[dict(path='runtime/plugin.c',text=source),dict(path='runtime/extra.h',text='#define EXTRA 10\n')]))
                    commit([dict(id=r['id'],enabled=r['id']==badid or (mode=='duplicate' and r['id']=='native-fixture')) for r in call('plugin.list')['items']])
                    original=(base/'home/config/plugins.json').read_bytes()
                    child.terminate();child.wait(timeout=10)
                    child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                    assert call('plugin.list')['recovery_mode'],mode
                    assert (base/'home/config/plugins.json').read_bytes()==original
                    commit([dict(id=badid,enabled=False)])
                    child.terminate();child.wait(timeout=10)
                    child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                    assert not call('plugin.list')['recovery_mode']
                # Resource readers, the runtime catalogue and editor ownership
                # must agree even when a real user file exists underneath.
                original_skill="---\nname: owned-skill\ndescription: Original fixture\n---\nOriginal instructions."
                target=base/'home/skills/owned-skill/SKILL.md';target.parent.mkdir(parents=True);target.write_text(original_skill,encoding='utf-8')
                plugin_skill=original_skill.replace('Original','Plugin')
                resource_doc=dict(format='mdo.plugin.v1',id='resource-owner',name='Resource owner',version='1.0.0',description='Owned resource fixture',overrides=[dict(source='skill.md',target='default-home/skills/owned-skill/SKILL.md')])
                call('plugin.apply',dict(id='resource-owner',manifest=resource_doc,files=[dict(path='skill.md',text=plugin_skill)]))
                assert commit([dict(id='resource-owner',enabled=True)])['restart_required']
                child.terminate();child.wait(timeout=10)
                child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                item=json.loads(request(port,'GET','/api/v1/extensions/skills/owned-skill')[2])['data']
                assert item['plugin_owned'] and item['content']==plugin_skill,item
                invoked=json.loads(request(port,'POST','/api/v1/skills/invoke',body=json.dumps(dict(id='owned-skill')).encode(),headers={'Content-Type':'application/json'})[2])['data'];assert 'Plugin instructions.' in invoked['text'],invoked
                for method in ['PUT','DELETE']:
                    code,_,body=request(port,method,'/api/v1/extensions/skills/owned-skill',body=json.dumps(dict(content=original_skill)).encode(),headers={'Content-Type':'application/json','If-Match':'"'+item['revision']+'"'})
                    assert code==403,(method,code,body)
                assert target.read_text(encoding='utf-8')==original_skill
                # Core overlays are compiled before native registration, rather
                # than merely looking enabled in a VFS read.
                core_source=(ROOT/'app/src/api/resources.c').read_text(encoding='utf-8').replace('MdoApiValueSetString(Data, "version", MDO_VERSION_TEXT)','MdoApiValueSetString(Data, "version", "core-fixture")')
                assert '"core-fixture"' in core_source
                core_doc=dict(format='mdo.plugin.v1',id='core-fixture',name='Core fixture',version='1.0.0',description='Core compilation fixture',overrides=[dict(source='core.c',target='src/api/resources.c')])
                call('plugin.apply',dict(id='core-fixture',manifest=core_doc,files=[dict(path='core.c',text=core_source)]))
                commit([dict(id='core-fixture',enabled=True)])
                child.terminate();child.wait(timeout=10)
                child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                bootstrap=json.loads(request(port,'GET','/api/v1/bootstrap')[2])['data']
                assert bootstrap['version']=='core-fixture',(bootstrap,(base/'probe.log').read_text(encoding='utf-8',errors='replace')[-2500:])
                assert call('qa.plugin.counter')==11,'Core delegation must not replay native Register'
                core_doc['version']='2.0.0'
                call('plugin.apply',dict(id='core-fixture',manifest=core_doc,files=[dict(path='core.c',text='this is not C;')]))
                child.terminate();child.wait(timeout=10)
                child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                assert call('plugin.list')['recovery_mode']
                assert json.loads(request(port,'GET','/api/v1/bootstrap')[2])['data']['version']=='0.7.0'
                call('qa.plugin.counter',status=404)
                # Corrupt registry recovery never overwrites the original file.
                registry=base/'home/config/plugins.json';registry.write_bytes(b'[{"id":null}]')
                child.terminate();child.wait(timeout=10)
                child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,child)
                assert call('plugin.list')['config_broken']
                call('plugin.apply',dict(id='should-not-save',manifest=core_doc,files=[dict(path='core.c',text='anything')]),409)
                assert registry.read_bytes()==b'[{"id":null}]'
                print('Plugins: immutable mappings, mutual exclusion, one-sided conflicts, dependency bounds/cycles, atomic switching and stale plans PASS',flush=True)
            finally:
                child.terminate();child.wait(timeout=10)


if __name__=='__main__':main()
