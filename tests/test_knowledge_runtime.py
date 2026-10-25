"""Real TCC storage, CAS, immutable-store controls and knowledge document preview."""
import argparse
import base64
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from test_api_runtime import free_port
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'home/tests'))
from test_mdo_delivery import request

from knowledge_fixture import metadata,file,package

def run():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'.build/host/windows-server-glibc/xs.exe');p.add_argument('--packed',action='store_true');p.add_argument('--ui',action='store_true');args=p.parse_args()
    # Keep absolute fixture paths short: the pinned Windows host's atomic
    # rename still has an absolute path limit, independent of tree depth.
    base=Path(tempfile.mkdtemp(prefix='k-',dir=ROOT/'.build' if os.name=='nt' else None));app=base/'app';home=base/'home';home.mkdir();app.mkdir() if args.packed else shutil.copytree(ROOT/'app',app)
    if args.packed:shutil.copy2(args.host,app/'mdo.exe');host=app/'mdo.exe'
    else:host=args.host.resolve()
    port=free_port();config=app/'xs.json';config.write_text(json.dumps(dict(services=[dict(enabled=True,name='knowledge',ip='127.0.0.1',port=port,**{'class':'http'},host_default=dict(enabled=True,name='mdo',path='web',devlang='c',devfile='generated/mdo_unity.c'))])))
    if not args.packed:
        install=app/'src/api/ecosystem_install.inc.c';code=install.read_text(encoding='utf-8').replace('    xstrview text;if(!EcoLocalAllowed(path)', '    if(strstr(path,"/fail-write.md"))return false;\n    xstrview text;if(!EcoLocalAllowed(path)');install.write_text(code,encoding='utf-8')
        router=app/'src/api/router.c';text=router.read_text(encoding='utf-8');text=text.replace('static const MdoApiRoute g_MdoApiRoutes[]', (ROOT/'tests/fixtures/knowledge-probe.c').read_text(encoding='utf-8')+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
        text=text.replace('g_MdoApiRoutes[] = {','g_MdoApiRoutes[] = {\n {"/api/v1/fixture/knowledge",XHTTP_METHOD_GET,"GET, OPTIONS",KnowledgeProbeRoute,false},');router.write_text(text,encoding='utf-8')
    def call(method,path,body=None,status=200,revision=None):
        headers={}
        if method!='GET':headers['X-Mdo-Write-Token']=request(port,'GET','/api/v1/account')[1]['X-Mdo-Write-Token']
        if revision:headers['If-Match']='"'+revision+'"'
        code,_,raw=request(port,method,'/api/v1'+path,body,headers=headers);assert code==status,(path,code,raw[:2000],str(base));return json.loads(raw).get('data')
    with (base/'runtime.log').open('wb')as log:
      proc=subprocess.Popen([str(host),*([] if args.packed else[str(config)])],cwd=app,env=dict(os.environ,MDO_HOME=str(home),USE_WEBVIEW='0'),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
      try:
        for _ in range(350):
            assert proc.poll()is None,str(base)
            try:
                if request(port,'GET','/api/v1/account')[0]==200:break
            except OSError:pass
            time.sleep(.1)
        if args.packed:
            code,_,asset=request(port,'GET','/js/features/settings/store-formats.js');assert code==200 and b'"plugins", "knowledge"].includes' in asset,'packed knowledge publisher is stale'
        assert call('GET','/extensions/knowledge')['items']==[]
        warnings=lambda:[d for d in call('GET','/diagnostics')['items']if d['domain']=='knowledge']
        assert not warnings(),'An optional absent knowledge directory is healthy'
        root_conflict=home/'knowledge';root_conflict.write_text('Preserve this existing file')
        assert [d['code']for d in warnings()]==['root_not_directory']
        sdk_warnings=call('POST','/application/call',dict(method='product.diagnostics.get',params={}))
        assert sdk_warnings['status']==200 and any(d.get('code')=='root_not_directory'for d in sdk_warnings['data']['items'])
        if not args.packed:
            degraded=call('GET','/fixture/knowledge');assert '<mdo_knowledge>'not in degraded['prompt']and not degraded['read']
        assert root_conflict.read_text()=='Preserve this existing file'
        root_conflict.unlink();assert not warnings(),'A fresh check clears a resolved condition'
        custom=metadata();files=[file('index.md','# Index\n[Topic](topic.md)'),file('topic.md','first version')]
        first=call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(custom),files=files),revision='new')
        sdk=call('POST','/application/call',dict(method='extensions.list',params=dict(path=dict(kind='knowledge'))));assert sdk['status']==200 and len(sdk['data']['items'])==1
        assert first['valid']and first['enabled']and first['inject_prompt']and first['directory']=='@knowledge/local-reference'
        assert not call('GET','/ecosystem'),'custom authoring does not leave an installation receipt'
        bundle=call('GET','/extensions/knowledge/local-reference/bundle');assert len(bundle['files'])==2 and bundle['bundle_revision']
        (home/'knowledge/local-reference/topic.md').write_text('changed outside app')
        call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(custom),files=files,bundle_revision=bundle['bundle_revision']),revision=first['revision'],status=412)
        assert(home/'knowledge/local-reference/topic.md').read_text()=='changed outside app'
        bundle=call('GET','/extensions/knowledge/local-reference/bundle');files[1]=file('topic.md','edited from UI')
        first=call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(custom),files=files,bundle_revision=bundle['bundle_revision']),revision=first['revision'])
        assert(home/'knowledge/local-reference/topic.md').read_text()=='edited from UI'
        if not args.packed:
            result=call('GET','/fixture/knowledge');assert result['prompt'].count('<mdo_knowledge>')==1 and '{$path}' not in result['prompt'] and '@knowledge/local-reference/reference/runtime.md' in result['prompt']
            assert result['read']and 'edited from UI'in result['read_text'] and result['grep']and result['glob']and not result['traversal']and not result['external']
        if not args.packed:
            before_receipts=(home/'data/extensions/installed.json').read_bytes();bundle=call('GET','/extensions/knowledge/local-reference/bundle')
            bad_files=[file('index.md','changed index'),file('topic.md','changed body'),file('fail-write.md','force fixture I/O failure')]
            call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(dict(custom,name='Rejected change')),files=bad_files,bundle_revision=bundle['bundle_revision']),revision=first['revision'],status=422)
            assert (home/'knowledge/local-reference/topic.md').read_text()=='edited from UI'
            assert json.loads((home/'knowledge/local-reference/knowledge.json').read_text())['name']==custom['name']
            assert (home/'data/extensions/installed.json').read_bytes()==before_receipts and not(home/'data/extensions/pending.json').exists()
        listing=call('GET','/extensions/knowledge/local-reference/documents');assert len(listing['files'])==2
        assert call('GET','/extensions/knowledge/local-reference/documents?path=topic.md')['text']=='edited from UI'
        call('GET','/extensions/knowledge/local-reference/documents?path=..%2F..%2Fconfig%2Faccount.json',status=400)
        call('GET','/extensions/knowledge/local-reference/documents?path=knowledge.json',status=400)
        disabled=call('POST','/extensions/knowledge/local-reference/enabled',dict(enabled=False),revision=first['revision']);assert not disabled['enabled']
        if not args.packed:assert '<mdo_knowledge>'not in call('GET','/fixture/knowledge')['prompt']
        first=call('POST','/extensions/knowledge/local-reference/enabled',dict(enabled=True),revision=first['revision']);assert first['enabled']
        without_hint=dict(custom,inject_prompt=False)
        first=call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(without_hint)),revision=first['revision'])
        if not args.packed:assert '<mdo_knowledge>'not in call('GET','/fixture/knowledge')['prompt']
        first=call('PUT','/extensions/knowledge/local-reference',dict(content=json.dumps(custom)),revision=first['revision'])
        if not args.packed:
            (home/'knowledge/broken').mkdir();(home/'knowledge/broken/knowledge.json').write_text('{invalid')
            assert 'Test base prompt'in call('GET','/fixture/knowledge')['prompt']
            assert any(d['code']=='invalid_metadata'and d['subject_id']=='broken'for d in warnings())
            (home/'knowledge/broken/knowledge.json').write_text(json.dumps(custom))
            assert any(d['code']=='index_unavailable'and d['subject_id']=='broken'for d in warnings())
            (home/'knowledge/broken/index.md').write_text('# Repaired')
            assert not warnings()
            preference=home/'config/extensions.json';saved_preference=preference.read_bytes()
            preference.write_text('{invalid')
            assert [d['code']for d in warnings()]==['config_unavailable'],'One shared preference failure must not become one warning per library'
            preference.write_bytes(saved_preference);assert not warnings()
        exported=call('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id='local-reference')]))
        assert exported['resources'][0]['files']==files or sorted(exported['resources'][0]['files'],key=lambda f:f['path'])==files
        # The writer has always allowed portable paths deeper than eight levels.
        # Exercise both a nine-level index and the longest installed file path,
        # including CAS edits, export and a complete package round trip.
        deep_index='/'.join(['level']*9+['index.md'])
        relative_limit=220-(11+len('deep'))
        levels=(relative_limit-4)//2
        deep_path='a/'*levels+'x'*(relative_limit-2*levels-3)+'.md'
        assert len(deep_path)==relative_limit and levels>8
        deep_doc={**metadata(),'index':deep_index}
        deep_files=[file(deep_index,'# Deep index'),file(deep_path,'Deep sentinel')]
        deep=call('PUT','/extensions/knowledge/deep',dict(content=json.dumps(deep_doc),files=deep_files),revision='new')
        assert deep['valid']
        inventory=call('GET','/extensions/knowledge/deep/documents')
        assert {f['path']for f in inventory['files']}=={deep_index,deep_path}
        from urllib.parse import quote
        assert call('GET','/extensions/knowledge/deep/documents?path='+quote(deep_path,safe=''))['text']=='Deep sentinel'
        bundle=call('GET','/extensions/knowledge/deep/bundle');assert len(bundle['files'])==2
        deep_files[1]=file(deep_path,'Edited deep sentinel')
        deep=call('PUT','/extensions/knowledge/deep',dict(content=json.dumps(deep_doc),files=deep_files,bundle_revision=bundle['bundle_revision']),revision=deep['revision'])
        exported_deep=call('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id='deep')]))
        assert sorted(exported_deep['resources'][0]['files'],key=lambda f:f['path'])==sorted(deep_files,key=lambda f:f['path'])
        outside=base/'outside';outside.mkdir();sentinel=outside/'private.md';sentinel.write_text('Outside sentinel')
        linked=home/'knowledge/deep/linked';has_link=False
        try:linked.symlink_to(outside,target_is_directory=True);has_link=True
        except OSError:
            if os.name=='nt':
                made=subprocess.run(['cmd','/c','mklink','/J',str(linked),str(outside)],capture_output=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                has_link=made.returncode==0
            if not has_link:print('SKIP link creation unavailable; rooted path traversal rejection remains covered')
        if has_link:
            call('GET','/extensions/knowledge/deep/documents',status=404)
            call('GET','/extensions/knowledge/deep/bundle',status=413)
            call('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id='deep')]),status=422)
        call('DELETE','/extensions/knowledge/deep',revision=deep['revision'])
        assert sentinel.read_text()=='Outside sentinel','cleanup must remove a link, never its target'
        assert not(home/'knowledge/deep').exists()
        deep_package=package();deep_package['manifest']['slug']='deep-reference';deep_package['resources']=exported_deep['resources']
        plan=call('POST','/ecosystem',dict(action='plan_import',single_kind='knowledge',package=deep_package))
        call('POST','/ecosystem',dict(action='import',single_kind='knowledge',package=deep_package,expected_plan=plan['sha256']))
        assert call('GET','/extensions/knowledge/deep/documents?path='+quote(deep_path,safe=''))['text']=='Edited deep sentinel'
        assert len(call('GET','/extensions/knowledge/deep/bundle')['files'])==2
        call('POST','/ecosystem',dict(action='uninstall',entry=dict(source='local',slug='deep-reference')))
        print('PASS nine-level index, maximum portable path, deep inventory, preview, bundle CAS, export and package round trip')
        large=call('PUT','/extensions/knowledge/large-reference',dict(content=json.dumps(custom),files=[file('index.md','# Large index'),file('large.md','x'*300000),dict(path='sample.bin',base64='AAEC/w==')]),revision='new')
        assert call('GET','/extensions/knowledge/large-reference/documents?path=large.md')['truncated']
        assert call('GET','/extensions/knowledge/large-reference/documents?path=sample.bin')['binary']
        call('DELETE','/extensions/knowledge/large-reference',revision=large['revision'])
        data=package()
        def store(action,**extra):return call('POST','/ecosystem',dict(action=action,single_kind='knowledge',**extra))
        plan=store('plan_import',package=data);assert len(plan['files'])==3
        store('import',package=data,expected_plan=plan['sha256'])
        # Turn the isolated local receipt into a downloaded-resource stand-in.
        receipt_path=home/'data/extensions/installed.json';records=json.loads(receipt_path.read_text());r=records.pop('local:xrt-reference');r.update(source='store',id=71,owner=7,submitter='fixture_author',key='71');records['71']=r;receipt_path.write_text(json.dumps(records))
        downloaded=next(v for v in call('GET','/extensions/knowledge')['items']if v['id']=='xrt-reference');assert downloaded['store_managed']and downloaded['submitter']=='fixture_author'
        call('PUT','/extensions/knowledge/xrt-reference',dict(content=downloaded.get('content','{}')),revision=downloaded['revision'],status=403)
        call('GET','/extensions/knowledge/xrt-reference/bundle',status=403)
        call('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id='xrt-reference')]),status=403)
        assert call('GET','/extensions/knowledge/xrt-reference/documents?path=reference%2Fruntime.md')['text']=='XRT knowledge sentinel'
        invalid=copy.deepcopy(data);invalid['resources'][0]['files'].append(file('../outside.txt','bad'))
        call('POST','/ecosystem',dict(action='plan_import',single_kind='knowledge',package=invalid),status=409)
        invalid=copy.deepcopy(data);invalid['resources'][0]['files'][0]['path']='other.md'
        call('POST','/ecosystem',dict(action='plan_import',single_kind='knowledge',package=invalid),status=409)
        call('DELETE','/extensions/knowledge/local-reference',revision=first['revision']);assert not(home/'knowledge/local-reference').exists()
        if args.ui:
            print('UI fixture',port,str(home),flush=True)
            while True:time.sleep(1)
        store('uninstall',entry=dict(id=71,source='store',slug='xrt-reference'));assert not(home/'knowledge/xrt-reference/knowledge.json').exists()
        print('PASS knowledge custom CRUD, folder CAS, native store provenance, portable path validation, document preview and uninstall',str(base))
      finally:proc.terminate();proc.wait(15)
if __name__=='__main__':run()
