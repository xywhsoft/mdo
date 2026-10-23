"""Portable command lifecycle through the real SDK, exec and spawn. No real
account/model calls; only an isolated native fixture and loopback tool store."""
import argparse,base64,hashlib,json,os,shutil,struct,subprocess,tempfile,threading,time,uuid
from pathlib import Path
from http.server import ThreadingHTTPServer
from test_api_runtime import ROOT,free_port,request,wait_ready,write_site
from test_application_sdk_runtime import COMMAND
import test_cli_tools_runtime as store

CHILD=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
int wmain(int argc,wchar_t** argv){
 if(argc>1&&!wcscmp(argv[1],L"--sleep")){puts("ready");fflush(stdout);Sleep(2500);return 37;}
 printf("VERSION\n");for(int i=1;i<argc;i++){char b[65536];int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,b,sizeof(b),0,0)-1;printf("%08x:",n);fwrite(b,1,n,stdout);putchar('\n');}
#else
#include <unistd.h>
int main(int argc,char** argv){
 if(argc>1&&!strcmp(argv[1],"--sleep")){puts("ready");fflush(stdout);sleep(3);return 37;}
 printf("VERSION\n");for(int i=1;i<argc;i++)printf("%08x:%s\n",(unsigned)strlen(argv[i]),argv[i]);
#endif
 char b[4096];size_t n;while((n=fread(b,1,sizeof(b),stdin)))fwrite(b,1,n,stdout);fputs("stderr-sentinel",stderr);return 37;
}
'''

def run(host,packed=None):
    base=Path(tempfile.mkdtemp(prefix='cli-bindings-',dir=ROOT/'.build'));home=base/'Home 中文';site=base/'site';port=free_port();config=write_site(site,port);store.stage_forwarder(site)
    (home/'commands').mkdir(parents=True);(home/'commands/sdk.c').write_text('/* mdo-command {"id":"sdk","description":"SDK fixture"} */\n#include <string.h>\n'+COMMAND,encoding='utf-8')
    source=base/'target.c';exe=base/('fixture.exe' if os.name=='nt' else 'fixture');versions={}
    for v in ('v1','v2'):
        source.write_text(CHILD.replace('VERSION',v),encoding='utf-8');subprocess.run(['gcc','-Os','-s',*(['-municode'] if os.name=='nt' else []),str(source),'-o',str(exe)],check=True);versions[v]=exe.read_bytes()
    def product(v,commands=None):
        payload=versions[v];entry='bin/'+exe.name;manifest=dict(store.MANIFEST,version='1.0.0' if v=='v1' else '2.0.0',entry=entry,commands=[dict(name='fixture',entry=entry,args=[]),dict(name='prefixed',entry=entry,args=['中文前缀',''])],files=[dict(path=entry,size=len(payload),sha256=hashlib.sha256(payload).hexdigest(),mode=493)])
        if commands is not None:manifest['commands']=commands
        text=json.dumps(manifest).encode();store.ARCHIVE=b'MDOCLI1\n'+struct.pack('<I',len(text))+text+payload;store.PRODUCT=dict(manifest,id=1,slug='fixture',tool_id='official.fixture',sha256=hashlib.sha256(store.ARCHIVE).hexdigest(),size=len(store.ARCHIVE),state='published')
    product('v1');server=ThreadingHTTPServer(('127.0.0.1',0),store.Store);threading.Thread(target=server.serve_forever,daemon=True).start()
    unity=site/'generated/mdo_unity.c';text=unity.read_text(encoding='utf-8');text=text.replace('http://127.0.0.1:9',f'http://127.0.0.1:{server.server_port}').replace('#define MDO_DISTRIBUTION_PORT 9\n',f'#define MDO_DISTRIBUTION_PORT {server.server_port}\n');text=f'#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:{server.server_port}"\n'+text;unity.write_text(text,encoding='utf-8')
    binding=site/'src/distribution/cli_bindings.inc.c';binding.write_text(binding.read_text(encoding='utf-8').replace('if(Ok)Ok=MdoDistSave("data/cli-tools/state.json",Saved);','if(Ok){xfile f=MdoHomeOpenRead("fail-cli-save");if(f){xrtClose(f);Ok=false;}else Ok=MdoDistSave("data/cli-tools/state.json",Saved);}'),encoding='utf-8')
    if packed:
        site=base/'packed-runtime';site.mkdir();shutil.copy2(packed,site/packed.name);config=site/'xs.json'
        config.write_text(json.dumps({'services':[{'class':'http','name':'mdo','enabled':True,'ip':'127.0.0.1','port':port,'host_default':{'name':'mdo','enabled':True,'path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}),encoding='utf-8')
    log=(base/'native.log').open('wb');proc=None
    def start():
        if packed:cmd=[str(site/packed.name),'--','--home',str(home)]
        else:cmd=[str(host),str(config),'--','--home',str(home)]
        p=subprocess.Popen(cmd,cwd=site,env={**os.environ,'MDO_HOME':str(home)},stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:wait_ready(port,p)
        except Exception as e:
            if p.poll() is None:p.terminate();p.wait(10)
            raise AssertionError((base/'native.log').read_text(encoding='utf-8',errors='replace')) from e
        return p
    def get():return json.loads(request(port,'GET','/api/v1/cli-tools')[2])['data']
    def post(action,status=200,**fields):
        code,_,raw=request(port,'POST','/api/v1/cli-tools',body=json.dumps(dict(action=action,**fields)).encode(),headers={'Content-Type':'application/json'});assert code==status,(code,raw);return json.loads(raw)
    def wait_task(expected='complete'):
        end=time.monotonic()+25
        while time.monotonic()<end:
            s=get()
            if not any(t['state'] in ('queued','running','installing','uploading') for t in s['tasks']):assert s['tasks'][-1]['state']==expected,s;return s
            time.sleep(.03)
        raise AssertionError(get())
    def invoke(method,params=None):
        identity='cmd-'+uuid.uuid4().hex;code,_,raw=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps(dict(invocation_id=identity,command_id='sdk',raw_arguments=json.dumps([method,params]))).encode(),headers={'Content-Type':'application/json'});assert code==202,(code,raw)
        end=time.monotonic()+15
        while time.monotonic()<end:
            value=json.loads(request(port,'GET','/api/v1/command-invocations/'+identity)[2])['data']
            if value['terminal']:assert value['state']=='succeeded',value;return value['result']
            time.sleep(.025)
        raise AssertionError((method,'SDK deadline'))
    def check(name='fixture',version='v1'):
        args=['','中文','a"b','tail\\','a&b'];r=invoke('process.run',dict(program=name,args=args,input='stdin-sentinel',cwd=str(base)))
        prefix=['中文前缀',''] if name=='prefixed' else []
        expected=version+'\n'+''.join(f'{len(a.encode()):08x}:{a}\n' for a in prefix+args)+'stdin-sentinel'
        assert r['exit_code']==37 and base64.b64decode(r['stdout']['base64']).decode().replace('\r\n','\n')==expected and base64.b64decode(r['stderr']['base64'])==b'stderr-sentinel',r
    try:
        proc=start()
        if not packed:post('install',store_id=1);s=wait_task()
        else:
            # Packed smoke uses a local import and never depends on store origin.
            exe.write_bytes(versions['v1']);tool=dict(id='custom.packed',name='Packed fixture',version='1.0.0',platform=store.PLATFORM,path=str(exe),prompt='fixture',license='MIT',enabled=True,aliases=['prefixed']);post('save',tool=tool);post('import',id=tool['id']);s=wait_task()
        check();
        if not packed:check('prefixed')
        saved=json.loads((home/'data/cli-tools/state.json').read_text(encoding='utf-8'));assert all(not r.get('path') and r['relative_path'] for r in saved['items']);assert all(not Path(r['target']).is_absolute() and ':' not in r['target'] for r in saved['commands'].values())
        session=invoke('session.create',{'body':{'project_id':'default','title':'CLI fixture','permission_profile':'full-access'}})['data']['id']
        for tool in ('exec','spawn'):
            r=invoke('tool.call',dict(project='default',session=session,tool=tool,arguments=dict(argv=['fixture','--sleep'],**({'expected_exit_codes':[37]} if tool=='exec' else {}))))
            assert r['success'],r
            if tool=='exec':assert 'ready' in r['text'],r
            else:
                import re
                task=int(re.search(r'task_id[=: ]+(\d+)',r['text']).group(1));r=invoke('tool.call',dict(project='default',session=session,tool='poll',arguments=dict(task_id=task,wait_ms=5000,release=True)));assert r['success'] and 'ready' in r['text'],r
        if not packed:
            # An active old target survives an update; the next invocation uses
            # the new registry without replacing the locked launcher image.
            alias=home/'data/cli-tools/bin'/('fixture.exe' if os.name=='nt' else 'fixture');running=subprocess.Popen([str(alias),'--sleep'],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert running.stdout.readline().strip()==b'ready'
            product('v2');post('install',store_id=1);wait_task();check(version='v2');assert running.wait(8)==37;running.stdout.close();running.stderr.close()
            entry='bin/'+exe.name;valid=dict(name='fixture',entry=entry,args=[]);before=get()['items'][0]['path']
            for bad in ([dict(valid,entry='missing')],[valid,valid],[dict(valid,name='con')],[dict(valid,args=[{}])]):
                product('v2',bad);post('install',store_id=1);wait_task('failed');assert get()['items'][0]['path']==before;check(version='v2')
            product('v2')
        item=get()['items'][0];key=item['id'];alias=home/'data/cli-tools/bin'/('fixture.exe' if os.name=='nt' else 'fixture');running=subprocess.Popen([str(alias),'--sleep'],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert running.stdout.readline().strip()==b'ready'
        post('enable',id=key,enabled=False);assert not get()['commands'];assert running.wait(8)==37;running.stdout.close();running.stderr.close();post('dismiss');assert not alias.exists(),'locked aliases must be removed on a later save';post('enable',id=key,enabled=True);check(version='v1' if packed else 'v2')
        # Protect arbitrary user files and name collisions; rejected edits do
        # not change either the running index or durable metadata.
        blocker=home/'data/cli-tools/bin'/('occupied.exe' if os.name=='nt' else 'occupied');blocker.write_bytes(b'user-file')
        extra=home/'own'/exe.name;extra.parent.mkdir();extra.write_bytes(versions['v1']);extra.chmod(0o755)
        custom=dict(id='custom.second',name='Custom',version='1.0.0',platform=store.PLATFORM,path=str(extra),prompt='fixture',license='MIT',enabled=True,command='fixture')
        post('save',422,tool=custom);assert len(get()['items'])==1
        custom['command']='occupied';post('save',422,tool=custom);assert blocker.read_bytes()==b'user-file'
        custom['command']='second';custom['aliases']=['con'];post('save',422,tool=custom);custom['aliases']=['other'];post('save',tool=custom);check('second');check('other')
        if not packed:
            replacement=home/'own'/'replacement';replacement.write_bytes(versions['v2']);replacement.chmod(0o755);(home/'fail-cli-save').write_bytes(b'1');post('save',422,tool=dict(custom,path=str(replacement)),revision=get()['revision']);(home/'fail-cli-save').unlink();check('second');check('other')
        post('remove',id=custom['id']);assert extra.exists();assert 'second' not in get()['commands']
        # Old archives lack a commands manifest. Their known companion entries
        # and BusyBox applet argv must still be exported without probing PATH.
        compat=home/'legacy SSH';compat.mkdir()
        for name in ('ssh','scp','sftp','ssh-keygen','ssh-keyscan','ssh-agent','ssh-add'):
            path=compat/(name+('.exe' if os.name=='nt' else ''));path.write_bytes(versions['v1']);path.chmod(0o755)
        post('save',tool=dict(custom,id='official.openssh',path=str(compat/('ssh.exe' if os.name=='nt' else 'ssh')),command='',aliases=[]));assert {'ssh','scp','sftp','ssh-keygen','ssh-keyscan','ssh-agent','ssh-add'}<=get()['commands'].keys();check('ssh-keygen');post('remove',id='official.openssh')
        busy=compat/('busybox.exe' if os.name=='nt' else 'busybox');busy.write_bytes(versions['v1']);busy.chmod(0o755)
        post('save',tool=dict(custom,id='official.busybox',path=str(busy),command='',aliases=[]));assert {'ls','pwd','echo','sh','grep'}<=get()['commands'].keys()
        r=invoke('process.run',dict(program='ls',input='stdin-sentinel'));assert r['exit_code']==37 and r['stdout']['text'].replace('\r\n','\n')=='v1\n00000002:ls\nstdin-sentinel',r;post('remove',id='official.busybox')
        if os.name!='nt':
            root=home/'scripts';(root/'bin').mkdir(parents=True);script=root/'bin/relative-tool';script.write_text('#!/bin/sh\nroot=${0%/*}/..\ncat "$root/data.txt"\n');script.chmod(0o755);(root/'data.txt').write_text('relative-runtime-ok')
            post('save',tool=dict(custom,id='custom.script',command='relative-tool',path=str(script),aliases=[]));r=invoke('process.run',{'program':'relative-tool'});assert r['stdout']['text']=='relative-runtime-ok',r;post('remove',id='custom.script')
        # Native executable imported from outside Home gains a relative mapping;
        # the original stays untouched, and reinitialization rebases all paths.
        external=base/('outside.exe' if os.name=='nt' else 'outside');external.write_bytes(versions['v1']);external.chmod(0o755)
        custom.update(id='custom.import',path=str(external),command='imported',aliases=[]);post('save',tool=custom);assert not get()['items'][-1]['portable'];post('import',id=custom['id']);wait_task();assert external.read_bytes()==versions['v1'];check('imported')
        proc.terminate();proc.wait(15);moved=base/'Moved Home';shutil.copytree(home,moved);home=moved;proc=start();check(version='v1' if packed else 'v2');check('imported')
        if not packed:
            proc.terminate();proc.wait(15);alias=home/'data/cli-tools/bin'/('fixture.exe' if os.name=='nt' else 'fixture');alias.unlink();alias.write_bytes(b'user replacement');proc=start();assert get()['writable'] and get()['binding_error'];post('enable',id=key,enabled=False);assert alias.read_bytes()==b'user replacement'
        post('remove',id=key);assert 'fixture' not in get()['commands'];assert external.exists()
        print('PASS portable CLI native lifecycle, SDK/process + exec/spawn, exact argv/streams/exit, prefixed aliases, running update, conflicts, custom import, relative-runtime scripts and moved Home')
    finally:
        if proc and proc.poll() is None:proc.terminate();proc.wait(15)
        log.close();server.shutdown();server.server_close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');p.add_argument('--packed',type=Path);a=p.parse_args();run(a.host,a.packed)
