"""Install all nine ecosystem systems without an online account, with rollback."""
import argparse,base64,copy,json,os,subprocess,tempfile,time
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready
from test_ecosystem_runtime import c_command

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='ecosystem-nine-',dir=ROOT/'.build') as tmp:
        base=Path(tmp);port=free_port();config=write_site(base,port);home=base/'home';home.mkdir()
        unity=base/'generated/mdo_unity.c'
        source='#define RequestProc ProductRequestProc\n'+unity.read_text(encoding='utf-8')
        source+='\n#undef RequestProc\nXS_RequestResult RequestProc(XS_HttpReq* R){if(xrtStrEqual(R->head->Target,XRT_STR_LITERAL("/fixture/environment"))){MdoApiContext C={0};C.Request=R;str P=xrtStrDup("fixture");if(MdoToolPrompt(&P))MdoApiReplySuccessTake(&C,200,xrtValueString(xrtStrView(P)),NULL);xrtFree(P);return XS_OK;}return ProductRequestProc(R);}\n'
        unity.write_text(source,encoding='utf-8')
        def app(method,path,body=None,status=200):
            code,_,raw=request(port,method,path,body=json.dumps(body).encode() if body is not None else None,headers={'Content-Type':'application/json'} if body is not None else {})
            data=json.loads(raw);assert code==status,(path,code,data);return data.get('data')
        def sdk(method,params):return app('POST','/api/v1/application/call',{'method':method,'params':params})
        def store(action,**kw):return app('POST','/api/v1/ecosystem',dict(action=action,**kw))
        def file(path,text):return {'path':path,'base64':base64.b64encode(text.encode()).decode()}
        tool=(ROOT/'tests/fixtures/modules/local-tool.c').read_text()
        mcp={'schema_version':1,'id':'eco-mcp','name':'QA MCP','description':'Offline MCP','enabled':False,
             'transport':{'type':'streamable-http','endpoint':'https://example.invalid/mcp','headers':[]},
             'protocol_version':'2026-07-28','startup_timeout_ms':5000,'request_timeout_ms':5000,
             'limits':{'message_bytes':1048576,'tools':16},'tools':{'allow':[],'deny':[]},
             'security':{'default_effects':['network','external-service'],'permission_profile':'balanced','trust_read_only_annotations':False},'auto_reconnect':True}
        resources=[
            {'kind':'agents','id':'eco-agent','content':'---\nname: QA Agent\ndescription: Offline agent\ntools: [read]\n---\nRead carefully.'},
            {'kind':'subagents','id':'eco-subagent','content':'---\nname: QA Child\ndescription: Offline child\ntools: [read]\n---\nReview carefully.'},
            {'kind':'tools','id':'eco-tool','content':tool.replace('user.fixture','user.ecotool').replace('user.tools.fixture','user.tools.ecotool')},
            {'kind':'skills','id':'eco-skill','content':'---\nname: eco-skill\ndescription: Offline skill\n---\nRead references/check.md','files':[file('references/check.md','original checklist')]},
            {'kind':'mcp','id':'eco-mcp','content':json.dumps(mcp)},
            {'kind':'commands','id':'eco-command','content':c_command('eco-command','Offline command')},
            {'kind':'modules','id':'eco-module','content':tool.replace('user.fixture','user.ecomodule').replace('user.tools.fixture','user.tools.ecomodule')},
            {'kind':'toolpacks','id':'eco-programs','content':json.dumps({'prompt':'ECO_USAGE_SENTINEL: run the listed QA program with Python.','programs':[{'name':'qa','path':'bin/qa.py','description':'Print an offline marker','interpreter':'python'}]}),'files':[file('bin/qa.py','print("ECO_PROGRAM_OK")\n')]},
            {'kind':'plugins','id':'eco-plugin','content':'{}','files':[file('web/ecosystem-qa.txt','ECO_PLUGIN_ONE')]},
        ]
        package={'format':'mdo.extension.v2','manifest':{'slug':'nine-suite','name':'Nine systems','version':'1.0.0','description':'Native nine-system installation','readme':'Offline QA','license':'MIT','platforms':['windows-x86_64'],'min_build_id':30000126},'resources':resources}
        with (base/'host.log').open('wb') as log:
            child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,
                                   env=dict(os.environ,USE_WEBVIEW='0',MDO_BUILD_ID='30000126'),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                try:wait_ready(port,child)
                except Exception as e:raise RuntimeError((base/'host.log').read_text(errors='replace')) from e
                assert app('GET','/api/v1/ecosystem')=={}
                plan=store('plan_import',package=package)
                assert len(plan['files'])>=9 and plan['reload_required'] and plan['configuration_required']
                assert not (home/'config/plugins.json').exists() and not (home/'agents/eco-agent.md').exists(),'plan must not publish files'
                app('POST','/api/v1/ecosystem',{'action':'import','package':package,'trust_code':True,'expected_plan':'0'*64},status=409)
                app('POST','/api/v1/ecosystem',{'action':'import','package':package},status=409)
                store('import',package=package,trust_code=True,expected_plan=plan['sha256'])
                installed=app('GET','/api/v1/ecosystem');record=installed['local:nine-suite'];assert len(record['resources'])==9,record
                assert record['activation']=='reload_required'
                assert sdk('vfs.read',{'path':'/app/web/ecosystem-qa.txt'})['text']=='ECO_PLUGIN_ONE'
                prompt=app('GET','/fixture/environment');assert 'ECO_USAGE_SENTINEL' in prompt and 'eco-programs.qa' in prompt,prompt
                registry=json.loads((home/'data/ecosystem/toolpacks/eco-programs.json').read_text());program=home/registry['root']/'bin/qa.py'
                assert subprocess.check_output([os.sys.executable,str(program)],text=True).strip()=='ECO_PROGRAM_OK'
                listed=store('c_sources')['items'];assert {'modules','toolpacks','plugins'} <= {r['kind'] for r in listed},listed
                exported=store('export',references=[{'kind':r['kind'],'id':r['id']} for r in resources])['resources']
                assert len(exported)==9 and exported[7]['files']==resources[7]['files']
                assert 'root' not in json.loads(exported[7]['content']), 'machine roots must never be published'
                app('POST','/api/v1/ecosystem',{'action':'import','package':package,'trust_code':True,'expected_plan':False},status=422)
                conflict=copy.deepcopy(package);conflict['manifest']['slug']='conflict-plugin';conflict['resources']=[copy.deepcopy(resources[8])];conflict['resources'][0]['id']='other-plugin'
                app('POST','/api/v1/ecosystem',{'action':'plan_import','package':conflict},status=409)
                before={p: (home/p).read_bytes() for p in ['config/plugins.json','data/extensions/installed.json','data/ecosystem/toolpacks/eco-programs.json']}
                broken=copy.deepcopy(package);broken['manifest']['version']='1.1.0'
                broken['resources'][6]['content']='INVALID C MODULE SOURCE'
                broken['resources'][8]['files']=[file('web/ecosystem-qa.txt','ECO_PLUGIN_BROKEN')]
                app('POST','/api/v1/ecosystem',{'action':'import','package':broken,'trust_code':True},status=409)
                assert all((home/p).read_bytes()==v for p,v in before.items()),'transaction did not restore all receipts and configuration'
                assert sdk('vfs.read',{'path':'/app/web/ecosystem-qa.txt'})['text']=='ECO_PLUGIN_ONE'
                assert not (home/'data/extensions/pending.json').exists()
                (home/'skills/eco-skill/references/check.md').write_text('locally edited')
                store('uninstall',entry={'source':'local','slug':'nine-suite','id':0})
                assert app('GET','/api/v1/ecosystem')=={}
                assert (home/'skills/eco-skill/SKILL.md').exists() and (home/'skills/eco-skill/references/check.md').read_text()=='locally edited'
                assert not (home/'data/ecosystem/toolpacks/eco-programs.json').exists()
                assert 'ECO_USAGE_SENTINEL' not in app('GET','/fixture/environment')
                app('POST','/api/v1/application/call',{'method':'vfs.read','params':{'path':'/app/web/ecosystem-qa.txt'}},status=404)
            finally:
                child.terminate();child.wait(timeout=10)
    print('PASS all nine systems offline install, command-line execution and prompt, plugin VFS, failed C rollback and uninstall preservation')

if __name__=='__main__':main()
